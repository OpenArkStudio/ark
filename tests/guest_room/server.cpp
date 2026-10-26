// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <arknet/external/asio.hpp>

#include "../../examples/guest_room/guest_room_protocol.hpp"

namespace {
using namespace std::chrono_literals;
using namespace ark::guest_room;
using tcp = asio::ip::tcp;

void require(bool condition, const std::string& reason) {
    if (!condition)
    {
        throw std::runtime_error(reason);
    }
}

struct launcher
{
    pid_t pid{-1};
    bool stopped{false};

    launcher(const std::string& program, const std::string& directory) {
        pid = fork();
        require(pid >= 0, "fork failed");
        if (pid == 0)
        {
            if (chdir(directory.c_str()) != 0)
            {
                _exit(126);
            }
            const int log = open("launcher.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (log < 0)
            {
                _exit(126);
            }
            dup2(log, STDOUT_FILENO);
            dup2(log, STDERR_FILENO);
            close(log);
            execl(program.c_str(), program.c_str(), "--busid=1.1.31.1", "--name=guest_room_native_test",
                "--app_conf=conf/guest_room.app.toml", "--log_conf=conf/log.conf", "--shutdown_timeout_ms=3000",
                static_cast<char*>(nullptr));
            _exit(127);
        }
    }

    void stop() {
        kill(pid, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (waitpid(pid, &status, WNOHANG) == pid)
            {
                stopped = true;
                require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "launcher failed normal shutdown");
                return;
            }
            std::this_thread::sleep_for(10ms);
        }
        throw std::runtime_error("launcher did not finish shutdown");
    }

    ~launcher() {
        if (pid > 0 && !stopped)
        {
            kill(pid, SIGKILL);
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }
};

struct client
{
    asio::io_context context;
    tcp::socket socket{context};

    explicit client(unsigned short port, bool retry = false) {
        const auto deadline = std::chrono::steady_clock::now() + (retry ? 5s : 1s);
        asio::error_code ec;
        do
        {
            socket.connect({asio::ip::make_address("127.0.0.1"), port}, ec);
            if (!ec)
            {
                socket.non_blocking(true);
                return;
            }
            socket.close();
            socket = tcp::socket(context);
            std::this_thread::sleep_for(10ms);
        } while (retry && std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("connect failed: " + ec.message());
    }

    void write(const std::string& bytes) {
        std::size_t offset = 0;
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (offset < bytes.size() && std::chrono::steady_clock::now() < deadline)
        {
            asio::error_code ec;
            offset += socket.write_some(asio::buffer(bytes.data() + offset, bytes.size() - offset), ec);
            require(!ec || ec == asio::error::would_block || ec == asio::error::try_again, "write failed");
            if (ec)
            {
                std::this_thread::sleep_for(2ms);
            }
        }
        require(offset == bytes.size(), "write timed out");
    }

    static std::string encode(ark::msg_id_t id, const std::string& body) {
        header head;
        head.set_message_id(id);
        head.set_message_length(static_cast<std::uint32_t>(body.size()));
        std::string bytes;
        head.serialize_to_string(bytes);
        return bytes + body;
    }

    void send(ark::msg_id_t id, const json& body) {
        write(encode(id, body.dump()));
    }

    std::string read(std::size_t count) {
        std::string bytes(count, '\0');
        std::size_t offset = 0;
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (offset < count && std::chrono::steady_clock::now() < deadline)
        {
            asio::error_code ec;
            offset += socket.read_some(asio::buffer(bytes.data() + offset, count - offset), ec);
            require(!ec || ec == asio::error::would_block || ec == asio::error::try_again,
                "unexpected disconnect while reading frame");
            if (ec)
            {
                std::this_thread::sleep_for(2ms);
            }
        }
        require(offset == count, "frame read timed out");
        return bytes;
    }

    json receive(ark::msg_id_t expected) {
        header head;
        const auto bytes = read(12);
        head.parse_from_array(bytes.data());
        require(head.message_id() == expected, "unexpected response id: " + std::to_string(head.message_id()));
        require(head.message_length() <= max_body, "invalid response header");
        return json::parse(read(head.message_length()));
    }

    void close() {
        asio::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
        socket.close(ec);
    }

    void expect_closed() {
        // Acknowledge the server FIN, so graceful transport shutdown can retire its native session.
        asio::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        char byte;
        while (std::chrono::steady_clock::now() < deadline)
        {
            socket.read_some(asio::buffer(&byte, 1), ec);
            if (ec == asio::error::eof || ec == asio::error::connection_reset)
            {
                socket.close();
                return;
            }
            require(!ec || ec == asio::error::would_block || ec == asio::error::try_again, "close read failed");
            std::this_thread::sleep_for(2ms);
        }
        throw std::runtime_error("invalid frame connection remained open");
    }
};

const json& player(const json& state, const std::string& id) {
    for (const auto& value : state["players"])
    {
        if (value["id"] == id)
        {
            return value;
        }
    }
    throw std::runtime_error("player missing from authoritative snapshot");
}

void error_is(client& peer, const std::string& code) {
    const auto body = peer.receive(error);
    require(body["code"] == code, "wrong error code");
    require(body["message"].get<std::string>().size() <= 160, "unbounded error text");
}

void exercise(unsigned short port) {
    client alice(port, true);
    alice.send(move, {{"seq", 1}, {"x", 0}, {"y", 0}});
    error_is(alice, "not_joined");
    const auto join_frame = client::encode(join, R"({"name":"Alice","room":"lobby"})");
    alice.write(join_frame.substr(0, 5));
    alice.write(join_frame.substr(5, 10));
    alice.write(join_frame.substr(15));
    const auto first_id = alice.receive(accepted)["player_id"].get<std::string>();
    require(alice.receive(snapshot)["players"].size() == 1, "first roster incorrect");
    alice.send(join, {{"name", "Again"}, {"room", "lobby"}});
    error_is(alice, "already_joined");

    client bob(port);
    bob.send(join, {{"name", "Bob"}, {"room", "lobby"}});
    const auto second_id = bob.receive(accepted)["player_id"].get<std::string>();
    require(first_id != second_id, "player IDs collided");
    require(bob.receive(snapshot)["players"].size() == 2, "joining peer missed full roster");
    require(alice.receive(snapshot)["players"].size() == 2, "existing peer missed join");

    alice.send(move, {{"seq", 1}, {"x", 120.0}, {"y", 100.0}});
    const auto own_move = alice.receive(snapshot);
    const auto other_move = bob.receive(snapshot);
    require(player(own_move, first_id)["x"] == 120.0, "own authoritative move missing");
    require(player(other_move, first_id)["seq"] == 1, "peer authoritative sequence missing");
    alice.send(move, {{"seq", 1}, {"x", 900}, {"y", 500}});
    error_is(alice, "stale_sequence");
    alice.send(move, {{"seq", 2}, {"x", -1}, {"y", 1}});
    error_is(alice, "invalid_message");
    alice.send(move, {{"seq", 1.5}, {"x", 0}, {"y", 0}});
    error_is(alice, "invalid_message");
    alice.write(client::encode(move, R"({"seq":2,"x":1e999,"y":0})"));
    error_is(alice, "invalid_message");
    alice.write(client::encode(move, "[]"));
    error_is(alice, "invalid_message");
    alice.write(client::encode(12345, "{}"));
    error_is(alice, "invalid_message");
    alice.write(client::encode(1, "{}"));
    error_is(alice, "invalid_message");
    alice.write(client::encode(ping, R"({"nonce":1})") + client::encode(ping, R"({"nonce":4294967295})"));
    require(alice.receive(pong)["nonce"] == 1, "first coalesced ping lost");
    require(alice.receive(pong)["nonce"] == 4294967295u, "second coalesced ping lost");

    bob.send(move, {{"seq", 1}, {"x", 900}, {"y", 500}});
    const auto bob_state = alice.receive(snapshot);
    require(player(bob_state, second_id)["x"] == 900, "second peer movement missing");
    require(player(bob_state, first_id)["x"] == 120.0, "invalid movement mutated entity");
    bob.receive(snapshot);
    bob.close();
    require(alice.receive(snapshot)["players"].size() == 1, "disconnect did not retire entity");

    client rejoined(port);
    rejoined.send(join, {{"name", "Bob"}, {"room", "lobby"}});
    const auto fresh_id = rejoined.receive(accepted)["player_id"].get<std::string>();
    require(fresh_id != second_id && fresh_id != first_id, "reconnect reused old identity");
    const auto fresh_roster = rejoined.receive(snapshot);
    require(fresh_roster["players"].size() == 2, "rejoin missed full roster");
    require(player(fresh_roster, fresh_id)["seq"] == 0, "rejoin retained old entity state");
    alice.receive(snapshot);
    rejoined.close();
    alice.receive(snapshot);
    alice.close();

    for (const auto kind : {0, 1, 2})
    {
        client malformed(port);
        auto bytes = client::encode(join, "{}").substr(0, 12);
        if (kind == 0)
        {
            bytes[0] = 'X';
        }
        if (kind == 1)
        {
            bytes[7] = 1;
        }
        if (kind == 2)
        {
            bytes[10] = 0x10;
            bytes[11] = 1;
        }
        malformed.write(bytes);
        malformed.expect_closed();
    }
    client names(port);
    names.send(join, {{"name", std::string(65, 'a')}, {"room", "lobby"}});
    error_is(names, "invalid_message");
    names.send(join, {{"name", "a\nb"}, {"room", "lobby"}});
    error_is(names, "invalid_message");
    names.write(client::encode(join, std::string("{\"name\":\"") + static_cast<char>(0xff) + "\",\"room\":\"lobby\"}"));
    error_is(names, "invalid_message");
    names.close();

    std::vector<std::unique_ptr<client>> capacity;
    for (int count = 0; count < 16; ++count)
    {
        auto peer = std::make_unique<client>(port);
        peer->send(join, {{"name", std::string(64, '"')}, {"room", "lobby"}});
        peer->receive(accepted);
        capacity.push_back(std::move(peer));
        for (const auto& member : capacity)
        {
            require(member->receive(snapshot)["players"].size() == capacity.size(), "capacity roster incorrect");
        }
    }
    client full(port);
    full.send(join, {{"name", "Overflow"}, {"room", "lobby"}});
    error_is(full, "room_full");
    full.close();
    for (const auto& member : capacity)
    {
        member->close();
    }
}
} // namespace

int main(int argc, char** argv) {
    try
    {
        require(argc == 4, "usage: server-test LAUNCHER RUNTIME_DIR PORT");
        launcher process(argv[1], argv[2]);
        exercise(static_cast<unsigned short>(std::stoul(argv[3])));
        process.stop();
        std::cout << "guest_room: actual launcher, Kernel entities, two peers, authoritative moves, errors, "
                     "split/coalesced frames, disconnect/rejoin, invalid framing and normal shutdown passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
