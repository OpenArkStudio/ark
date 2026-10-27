#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "doctest.h"
#include <arknet/rudp/rudp_client.hpp>

#include "ark/net/rudp_client.hpp"
#include "ark/net/rudp_server.hpp"
#include "body_limit_header.hpp"

namespace {

using namespace std::chrono_literals;

struct rudp_message
{
    ark::msg_id_t id;
    ark::conv_id_t session;
    std::string body;
    ark::guid_t actor;
    ark::bus_id_t source;
    ark::bus_id_t destination;
    bool compressed;
};

struct rudp_event
{
    ark::net_event_type type;
    ark::conv_id_t session;
    ark::bus_id_t bus;
    std::string ip;
};

class rudp_receiver
{
public:
    void receive(const ark::net_msg* message, ark::conv_id_t session) {
        {
            std::lock_guard lock(mutex_);
            wrong_thread_ |= std::this_thread::get_id() != update_thread_;
            messages_.push_back({message->get_msg_id(), session,
                message->get_msg_length() ? std::string(message->get_msg_data(), message->get_msg_length())
                                          : std::string{},
                message->get_actor_id(), message->get_src_bus(), message->get_dst_bus(), message->get_compress() != 0});
        }
        if (on_receive)
        {
            on_receive(message, session);
        }
    }

    void event(const ark::net_event* event) {
        std::lock_guard lock(mutex_);
        wrong_thread_ |= std::this_thread::get_id() != update_thread_;
        events_.push_back({event->get_type(), event->get_id(), event->get_bus_id(), event->get_ip()});
    }

    std::vector<rudp_message> messages() {
        std::lock_guard lock(mutex_);
        return messages_;
    }

    std::vector<rudp_event> events() {
        std::lock_guard lock(mutex_);
        return events_;
    }

    std::size_t event_count(ark::net_event_type type) {
        std::lock_guard lock(mutex_);
        return static_cast<std::size_t>(std::count_if(
            events_.begin(), events_.end(), [type](const rudp_event& value) { return value.type == type; }));
    }

    bool wrong_thread() {
        std::lock_guard lock(mutex_);
        return wrong_thread_;
    }

    std::function<void(const ark::net_msg*, ark::conv_id_t)> on_receive;

private:
    const std::thread::id update_thread_ = std::this_thread::get_id();
    std::mutex mutex_;
    std::vector<rudp_message> messages_;
    std::vector<rudp_event> events_;
    bool wrong_thread_ = false;
};

std::uint16_t reserve_rudp_port() {
    asio::io_context context;
    asio::ip::udp::socket socket(context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
    return socket.local_endpoint().port();
}

template<class predicate_type, class update_type>
bool await_rudp(predicate_type&& predicate, update_type&& update, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do
    {
        update();
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

std::string rudp_payload(std::size_t size) {
    std::string result(size, '\0');
    for (std::size_t index = 0; index < size; ++index)
    {
        result[index] = static_cast<char>((index * 19 + 37) & 0xff);
    }
    return result;
}

template<class header_type>
std::shared_ptr<header_type> rudp_header() {
    auto header = std::make_shared<header_type>();
    header->set_compressed(true);
    header->set_actor_id(0x1234567812345678);
    header->set_source_bus_id(101);
    header->set_dest_bus_id(202);
    return header;
}

std::shared_ptr<ark::rudp_client> rudp_client(rudp_receiver& receiver) {
    return std::make_shared<ark::rudp_client>(&receiver, &rudp_receiver::receive, &rudp_receiver::event);
}

std::shared_ptr<ark::rudp_server> rudp_server(rudp_receiver& receiver) {
    return std::make_shared<ark::rudp_server>(&receiver, &rudp_receiver::receive, &rudp_receiver::event);
}

class rudp_serialization_header : public ark::s_msg_header
{
public:
    void serialize_to_string(std::string& buffer) override {
        ark::s_msg_header::serialize_to_string(buffer);
        if (auto callback = std::exchange(on_serialize, {}))
        {
            callback();
        }
    }

    std::function<void()> on_serialize;
};
} // namespace

TEST_CASE_TEMPLATE("RUDP preserves ARK framing and dispatches on Update", header_type, ark::s_msg_header,
    ark::ss_msg_header, ark::test::large_body_header<ark::s_msg_header>,
    ark::test::large_body_header<ark::ss_msg_header>) {
    rudp_receiver client_receiver;
    rudp_receiver server_receiver;
    auto client = rudp_client(client_receiver);
    auto server = rudp_server(server_receiver);
    auto header = rudp_header<header_type>();
    const auto port = reserve_rudp_port();
    REQUIRE(server->start_server(header, 101, "127.0.0.1", port, 2, 4, 30000));
    REQUIRE(client->start_client(header, 202, "127.0.0.1", port));
    CHECK(client_receiver.events().empty());
    CHECK(server_receiver.events().empty());
    const auto update = [&] {
        server->update();
        client->update();
    };
    REQUIRE(await_rudp(
        [&] { return client_receiver.events().size() == 1 && server_receiver.events().size() == 1; }, update));
    const auto client_session = client_receiver.events().front().session;
    const auto server_session = server_receiver.events().front().session;
    CHECK(client_receiver.events().front().bus == 202);
    CHECK(server_receiver.events().front().bus == 101);
    CHECK(server->get_ip(server_session) == "127.0.0.1");
    CHECK(server->get_ip(server_session + 100) == ark::null_str);

    std::size_t rejected_echoes = 0;
    server_receiver.on_receive = [&](const ark::net_msg* message, ark::conv_id_t session) {
        header_type response(*header);
        response.set_message_id(message->get_msg_id());
        response.set_message_length(message->get_msg_length());
        if (!server->send_msg(&response, message->get_msg_length() ? message->get_msg_data() : "", session))
        {
            ++rejected_echoes;
        }
    };
    const std::vector<std::size_t> sizes{0, 1, 23, 511, 1024, header->max_body_size()};
    for (std::size_t index = 0; index < sizes.size(); ++index)
    {
        header_type request(*header);
        request.set_message_id(static_cast<ark::msg_id_t>(40 + index));
        request.set_message_length(static_cast<std::uint32_t>(sizes[index]));
        const auto body = rudp_payload(sizes[index]);
        REQUIRE(client->send_msg(&request, body.empty() ? "" : body.data(), client_session));
    }
    CHECK(client_receiver.messages().empty());
    CHECK(server_receiver.messages().empty());
    REQUIRE(await_rudp(
        [&] {
            return client_receiver.messages().size() == sizes.size() &&
                   server_receiver.messages().size() == sizes.size();
        },
        update));
    CHECK(rejected_echoes == 0);
    const auto sent = server_receiver.messages();
    const auto echoed = client_receiver.messages();
    for (std::size_t index = 0; index < sizes.size(); ++index)
    {
        CHECK(sent[index].id == 40 + index);
        CHECK(echoed[index].id == sent[index].id);
        CHECK(sent[index].body == rudp_payload(sizes[index]));
        CHECK(echoed[index].body == sent[index].body);
        CHECK(sent[index].session == server_session);
        CHECK(echoed[index].session == client_session);
        CHECK(sent[index].actor == header->actor_id());
        CHECK(sent[index].source == header->source_bus_id());
        CHECK(sent[index].destination == header->dest_bus_id());
        CHECK(sent[index].compressed);
    }

    header_type broadcast(*header);
    broadcast.set_message_id(99);
    const auto body = rudp_payload(header->max_body_size());
    broadcast.set_message_length(static_cast<std::uint32_t>(body.size()));
    server->broadcast_msg(&broadcast, body.data());
    REQUIRE(await_rudp([&] { return client_receiver.messages().size() == sizes.size() + 1; }, update));
    CHECK(client_receiver.messages().back().id == 99);
    CHECK(client_receiver.messages().back().body == body);
    broadcast.set_message_length(header->max_body_size() + 1);
    CHECK_FALSE(client->send_msg(&broadcast, body.data(), client_session));
    CHECK_FALSE(server->send_msg(&broadcast, body.data(), server_session));
    server->close_session(server_session);
    CHECK(server_receiver.event_count(ark::net_event_type::disconnect) == 0);
    REQUIRE(await_rudp(
        [&] {
            return client_receiver.event_count(ark::net_event_type::disconnect) == 1 &&
                   server_receiver.event_count(ark::net_event_type::disconnect) == 1;
        },
        update));
    CHECK_FALSE(client_receiver.wrong_thread());
    CHECK_FALSE(server_receiver.wrong_thread());
    client->shutdown();
    server->shutdown();
}

TEST_CASE("RUDP rejects invalid sends and unstarted endpoints shut down safely") {
    rudp_receiver receiver;
    auto client = rudp_client(receiver);
    auto server = rudp_server(receiver);
    auto header = rudp_header<ark::s_msg_header>();
    header->set_message_id(10);
    header->set_message_length(0);
    CHECK_FALSE(client->send_msg(header.get(), "", 0));
    CHECK_FALSE(server->send_msg(header.get(), "", 1000));
    CHECK_FALSE(client->send_msg(nullptr, "", 0));
    CHECK_FALSE(server->send_msg(header.get(), nullptr, 1000));
    CHECK_FALSE(server->start_server(header, 1, "127.0.0.1", reserve_rudp_port(), 1, 0, 1000));
    client->shutdown();
    server->shutdown();
    client->update();
    server->update();
    CHECK_FALSE(client->is_working());
    CHECK_FALSE(server->is_working());
    CHECK(receiver.events().empty());
}

TEST_CASE("RUDP wrapper rejects stale session IDs and oversized messages") {
    rudp_receiver client_receiver;
    rudp_receiver server_receiver;
    auto client = rudp_client(client_receiver);
    auto server = rudp_server(server_receiver);
    auto header = rudp_header<ark::s_msg_header>();
    const auto port = reserve_rudp_port();
    REQUIRE(server->start_server(header, 1, "127.0.0.1", port, 1, 1, 30000));
    REQUIRE(client->start_client(header, 2, "127.0.0.1", port));
    const auto update = [&] {
        server->update();
        client->update();
    };
    REQUIRE(await_rudp(
        [&] { return client_receiver.events().size() == 1 && server_receiver.events().size() == 1; }, update));
    header->set_message_id(10);
    header->set_message_length(0);
    CHECK_FALSE(client->send_msg(header.get(), "", client_receiver.events().front().session + 1));
    CHECK_FALSE(server->send_msg(header.get(), "", server_receiver.events().front().session + 1));
    header->set_message_length(4097);
    const auto oversized = rudp_payload(4097);
    CHECK_FALSE(client->send_msg(header.get(), oversized.data(), 0));
    CHECK_FALSE(server->send_msg(header.get(), oversized.data(), server_receiver.events().front().session));
    server->broadcast_msg(header.get(), oversized.data());
    client->shutdown();
    header->set_message_length(0);
    CHECK_FALSE(client->send_msg(header.get(), "", 0));
    server->shutdown();
    update();
    CHECK(client_receiver.messages().empty());
    CHECK(server_receiver.messages().empty());
}

TEST_CASE("RUDP reconnects after a server restart and retains queued lifecycle events") {
    rudp_receiver client_receiver;
    rudp_receiver server_receiver;
    auto client = rudp_client(client_receiver);
    auto server = rudp_server(server_receiver);
    auto header = rudp_header<ark::ss_msg_header>();
    const auto port = reserve_rudp_port();
    REQUIRE(server->start_server(header, 1, "127.0.0.1", port, 2, 2, 30000));
    REQUIRE(client->start_client(header, 2, "127.0.0.1", port));
    server->shutdown();
    REQUIRE(server->start_server(header, 1, "127.0.0.1", port, 2, 2, 30000));
    const auto update = [&] {
        server->update();
        client->update();
    };
    REQUIRE(await_rudp(
        [&] {
            return client_receiver.event_count(ark::net_event_type::connect) == 2 &&
                   client_receiver.event_count(ark::net_event_type::disconnect) == 1 &&
                   server_receiver.event_count(ark::net_event_type::connect) == 2;
        },
        update, 12s));
    const auto client_events = client_receiver.events();
    REQUIRE(client_events.size() == 3);
    CHECK(client_events[0].type == ark::net_event_type::connect);
    CHECK(client_events[1].type == ark::net_event_type::disconnect);
    CHECK(client_events[2].type == ark::net_event_type::connect);
    CHECK(client_events[0].session != client_events[2].session);
    CHECK(server_receiver.event_count(ark::net_event_type::disconnect) == 1);
    header->set_message_id(71);
    header->set_message_length(1);
    REQUIRE(client->send_msg(header.get(), "x", client_events.back().session));
    REQUIRE(await_rudp([&] { return server_receiver.messages().size() == 1; }, update));
    CHECK(server_receiver.messages().front().body == "x");
    CHECK_FALSE(client_receiver.wrong_thread());
    CHECK_FALSE(server_receiver.wrong_thread());
    client->shutdown();
    server->shutdown();
}

TEST_CASE("RUDP server admission obeys max_client and releases disconnected slots") {
    rudp_receiver client_receiver;
    rudp_receiver server_receiver;
    auto client = rudp_client(client_receiver);
    auto server = rudp_server(server_receiver);
    auto header = rudp_header<ark::s_msg_header>();
    const auto port = reserve_rudp_port();
    REQUIRE(server->start_server(header, 1, "127.0.0.1", port, 2, 1, 30000));
    REQUIRE(client->start_client(header, 2, "127.0.0.1", port));
    const auto update = [&] {
        server->update();
        client->update();
    };
    REQUIRE(await_rudp([&] { return server_receiver.events().size() == 1; }, update));
    arknet::rudp_client excess;
    excess.set_auto_reconnect(false).set_connect_timeout(300ms);
    CHECK_FALSE(excess.start("127.0.0.1", port));
    CHECK(arknet::get_last_error() == asio::error::timed_out);
    update();
    CHECK(server_receiver.event_count(ark::net_event_type::connect) == 1);
    client->shutdown();
    REQUIRE(await_rudp([&] { return server_receiver.event_count(ark::net_event_type::disconnect) == 1; }, update));
    excess.set_connect_timeout(2s);
    REQUIRE(excess.start("127.0.0.1", port));
    REQUIRE(await_rudp([&] { return server_receiver.event_count(ark::net_event_type::connect) == 2; }, update));
    excess.stop();
    server->shutdown();
}

TEST_CASE("RUDP server silence timeout is configured in milliseconds") {
    rudp_receiver client_receiver;
    rudp_receiver server_receiver;
    auto client = rudp_client(client_receiver);
    auto server = rudp_server(server_receiver);
    auto header = rudp_header<ark::s_msg_header>();
    const auto port = reserve_rudp_port();
    REQUIRE(server->start_server(header, 1, "127.0.0.1", port, 1, 1, 80));
    REQUIRE(client->start_client(header, 2, "127.0.0.1", port));
    const auto update = [&] {
        server->update();
        client->update();
    };
    REQUIRE(await_rudp([&] { return server_receiver.event_count(ark::net_event_type::disconnect) == 1; }, update, 2s));
    CHECK_FALSE(server_receiver.wrong_thread());
    client->shutdown();
    server->shutdown();
}

TEST_CASE("RUDP endpoint callbacks do not keep ARK wrappers alive") {
    rudp_receiver client_receiver;
    rudp_receiver server_receiver;
    auto client = rudp_client(client_receiver);
    auto server = rudp_server(server_receiver);
    auto header = rudp_header<ark::s_msg_header>();
    const auto port = reserve_rudp_port();
    REQUIRE(server->start_server(header, 1, "127.0.0.1", port, 1, 1, 30000));
    REQUIRE(client->start_client(header, 2, "127.0.0.1", port));
    std::weak_ptr<ark::rudp_client> weak_client = client;
    std::weak_ptr<ark::rudp_server> weak_server = server;
    client.reset();
    server.reset();
    REQUIRE(await_rudp([&] { return weak_client.expired() && weak_server.expired(); }, [] {}, 2s));
    CHECK(client_receiver.events().empty());
    CHECK(server_receiver.events().empty());
}

TEST_CASE("RUDP closes malformed business frames through the Update event path") {
    auto header = rudp_header<ark::s_msg_header>();
    ark::s_msg_header invalid;
    invalid.set_message_id(100);
    invalid.set_message_length(4097);
    std::string wire;
    invalid.serialize_to_string(wire);
    const auto port = reserve_rudp_port();

    SUBCASE("ARK server") {
        rudp_receiver receiver;
        auto server = rudp_server(receiver);
        REQUIRE(server->start_server(header, 1, "127.0.0.1", port, 2, 1, 30000));
        arknet::rudp_client client;
        client.set_auto_reconnect(false);
        REQUIRE(client.start("127.0.0.1", port));
        REQUIRE(client.async_send(wire));
        REQUIRE(await_rudp(
            [&] { return receiver.event_count(ark::net_event_type::disconnect) == 1; }, [&] { server->update(); }));
        CHECK(receiver.event_count(ark::net_event_type::connect) == 1);
        CHECK(receiver.messages().empty());
        CHECK_FALSE(receiver.wrong_thread());
        client.stop();
        server->shutdown();
    }

    SUBCASE("ARK client") {
        rudp_receiver receiver;
        auto client = rudp_client(receiver);
        arknet::rudp_server server;
        std::atomic<bool> rejected = false;
        server.bind_connect([&](auto& session) {
            if (!session->async_send(wire))
            {
                rejected = true;
            }
        });
        REQUIRE(server.start("127.0.0.1", port));
        REQUIRE(client->start_client(header, 2, "127.0.0.1", port));
        REQUIRE(await_rudp(
            [&] { return receiver.event_count(ark::net_event_type::disconnect) == 1; }, [&] { client->update(); }));
        CHECK_FALSE(rejected.load());
        CHECK(receiver.event_count(ark::net_event_type::connect) == 1);
        CHECK(receiver.messages().empty());
        CHECK_FALSE(receiver.wrong_thread());
        client->shutdown();
        server.stop();
    }
}

TEST_CASE("RUDP rejects an old-session send after reconnect during serialization") {
    std::mutex peer_mutex;
    std::shared_ptr<arknet::rudp_session> peer;
    std::vector<std::string> messages;
    arknet::rudp_server server;
    rudp_receiver receiver;
    auto client = rudp_client(receiver);
    auto header = rudp_header<ark::s_msg_header>();
    server.bind_connect([&](auto& session) {
        std::lock_guard lock(peer_mutex);
        peer = session;
    });
    server.bind_recv([&](auto&, std::string_view bytes) {
        std::lock_guard lock(peer_mutex);
        messages.emplace_back(bytes);
    });
    REQUIRE(server.start("127.0.0.1", 0));
    REQUIRE(client->start_client(header, 2, "127.0.0.1", server.listen_port()));
    REQUIRE(
        await_rudp([&] { return receiver.event_count(ark::net_event_type::connect) == 1; }, [&] { client->update(); }));
    const auto old_id = receiver.events().back().session;

    rudp_serialization_header request;
    request.set_message_id(71);
    request.set_message_length(1);
    bool reconnected = false;
    request.on_serialize = [&] {
        std::shared_ptr<arknet::rudp_session> old_peer;
        {
            std::lock_guard lock(peer_mutex);
            old_peer = peer;
        }
        if (!old_peer)
        {
            return;
        }
        old_peer->stop();
        reconnected = await_rudp(
            [&] { return receiver.event_count(ark::net_event_type::connect) == 2; }, [&] { client->update(); }, 12s);
    };
    CHECK_FALSE(client->send_msg(&request, "x", old_id));
    REQUIRE(reconnected);
    const auto new_id = receiver.events().back().session;
    REQUIRE(new_id != old_id);

    client->close_session(old_id);
    request.set_message_id(72);
    REQUIRE(client->send_msg(&request, "n", new_id));
    std::string expected;
    request.serialize_to_string(expected);
    expected.append("n");
    REQUIRE(await_rudp(
        [&] {
            std::lock_guard lock(peer_mutex);
            return std::find(messages.begin(), messages.end(), expected) != messages.end();
        },
        [&] { client->update(); }));
    {
        std::lock_guard lock(peer_mutex);
        REQUIRE(messages.size() == 1);
        CHECK(messages.front() == expected);
    }
    client->close_session(0);
    REQUIRE(await_rudp(
        [&] { return receiver.event_count(ark::net_event_type::disconnect) == 2; }, [&] { client->update(); }));
    CHECK_FALSE(client->send_msg(&request, "n", new_id));
    client->shutdown();
    server.stop();
}
