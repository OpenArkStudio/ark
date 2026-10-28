// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <arknet/external/asio.hpp>
#include <nlohmann/json.hpp>

#include "../../examples/guest_room/guest_room_wire.hpp"

namespace {
using namespace std::chrono_literals;
using namespace ark::guest_room;
using json = nlohmann::json;
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

    launcher(const std::string& program, const std::string& directory, const std::string& log_name = "launcher.log") {
        pid = fork();
        require(pid >= 0, "fork failed");
        if (pid == 0)
        {
            if (chdir(directory.c_str()) != 0)
            {
                _exit(126);
            }
            const int log = open(log_name.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
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

    void expect_failure(const std::filesystem::path& log_path) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (waitpid(pid, &status, WNOHANG) == pid)
            {
                stopped = true;
                require(WIFEXITED(status) && WEXITSTATUS(status) == 1, "invalid profile did not fail normal startup");
                std::ifstream log(log_path);
                const std::string text{std::istreambuf_iterator<char>(log), std::istreambuf_iterator<char>()};
                require(text.find("profile 1 requires bounded HP/MP, positive maxima and zero initial sequences") !=
                            std::string::npos,
                    "invalid profile lacked an application configuration diagnostic");
                return;
            }
            std::this_thread::sleep_for(10ms);
        }
        throw std::runtime_error("invalid profile launcher did not exit");
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

    template<class Message>
    void send(ark::msg_id_t id, const Message& body) {
        write(encode(id, body.SerializeAsString()));
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

    template<class Message>
    Message receive(ark::msg_id_t expected) {
        header head;
        const auto bytes = read(12);
        head.parse_from_array(bytes.data());
        require(head.message_id() == expected,
            "unexpected response id: " + std::to_string(head.message_id()) + " expected " + std::to_string(expected));
        require(head.message_length() <= max_body, "invalid response header");
        Message body;
        require(body.ParseFromString(read(head.message_length())), "malformed protobuf response");
        return body;
    }

    void quiet() {
        std::this_thread::sleep_for(50ms);
        asio::error_code ec;
        require(socket.available(ec) == 0 && !ec, "unexpected frame after coalesced/no-op flush");
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

wire::join_request join_as(const std::string& name) {
    wire::join_request body;
    body.set_name(name);
    body.set_room("lobby");
    body.set_protocol_version(protocol_version);
    return body;
}

wire::move_request move_to(std::uint32_t sequence, double x, double y) {
    wire::move_request body;
    body.set_sequence(sequence);
    body.set_x(x);
    body.set_y(y);
    return body;
}

wire::action_request act(std::uint32_t sequence, wire::action_kind kind) {
    wire::action_request body;
    body.set_sequence(sequence);
    body.set_kind(kind);
    return body;
}

void error_is(client& peer, const std::string& code, std::optional<std::uint32_t> sequence = std::nullopt) {
    const auto body = peer.receive<wire::error_response>(error);
    require(body.code() == code && body.message().size() <= 160, "wrong error response");
    require(body.has_action_sequence() == sequence.has_value(), "wrong error correlation presence");
    if (sequence)
    {
        require(body.action_sequence() == *sequence, "wrong action error correlation");
    }
}

struct roster
{
    std::map<ark::guid_t, std::unique_ptr<ark::entity_sync::scalar_replica>> players;
    std::map<std::string, ark::guid_t> identities;

    void add(const wire::player_baseline& player) {
        require(!player.player_id().empty() && player.has_state(), "missing baseline identity");
        const auto baseline = decode(player.state(), player_limits);
        validate_player(baseline);
        const auto id = baseline.snapshot.entity_id;
        require(!players.contains(id) && !identities.contains(player.player_id()), "duplicate identity");
        auto replica =
            std::make_unique<ark::entity_sync::scalar_replica>(player_schema, id, "GuestPlayer", player_limits);
        require(replica->apply(baseline) == ark::entity_sync::scalar_apply_result::applied, "baseline rejected");
        identities.emplace(player.player_id(), id);
        players.emplace(id, std::move(replica));
    }

    void baseline(client& peer) {
        const auto body = peer.receive<wire::room_baseline>(snapshot);
        require(body.room() == "lobby", "wrong baseline room");
        players.clear();
        identities.clear();
        for (const auto& value : body.players())
        {
            add(value);
        }
    }

    wire::room_delta changes(client& peer) {
        const auto body = peer.receive<wire::room_delta>(delta);
        require(body.room() == "lobby", "wrong delta room");
        for (const auto& value : body.added())
        {
            add(value);
        }
        for (const auto& value : body.changed())
        {
            const auto change = decode(value, player_limits);
            require(players.contains(change.entity_id), "delta references unknown Entity");
            require(players.at(change.entity_id)->apply(change) == ark::entity_sync::scalar_apply_result::applied,
                "delta gap, invalid fields or stale generation");
        }
        return body;
    }

    const ark::entity_sync::scalar_snapshot& player(const std::string& id) const {
        const auto state = players.at(identities.at(id))->state();
        require(state != nullptr, "player removed");
        return *state;
    }

    std::uint32_t number(const std::string& id, std::uint32_t index) const {
        return std::get<std::uint32_t>(player(id).fields.at(index));
    }
};

void exercise(unsigned short port) {
    client alice(port, true);
    alice.send(move, move_to(1, 0, 0));
    error_is(alice, "not_joined");
    const auto join_frame = client::encode(join, join_as("Alice").SerializeAsString());
    alice.write(join_frame.substr(0, 5));
    alice.write(join_frame.substr(5, 10));
    alice.write(join_frame.substr(15));
    const auto first = alice.receive<wire::join_accepted>(accepted);
    require(first.protocol_version() == protocol_version && first.entity_id() > 0, "missing protocol/Entity binding");
    roster own;
    own.baseline(alice);
    require(
        own.players.size() == 1 && own.identities.at(first.player_id()) == static_cast<ark::guid_t>(first.entity_id()),
        "first roster or Accepted binding incorrect");
    alice.send(join, join_as("Again"));
    error_is(alice, "already_joined");

    client bob(port);
    bob.send(join, join_as("Bob"));
    const auto second = bob.receive<wire::join_accepted>(accepted);
    require(first.player_id() != second.player_id() && first.entity_id() != second.entity_id(), "identities collided");
    roster other;
    other.baseline(bob);
    require(other.players.size() == 2, "late join missed full roster");
    const auto added = own.changes(alice);
    require(added.added_size() == 1 && added.changed_size() == 0, "join must send added baseline only");

    // Both commands are in one transport write. No full baseline may appear during movement.
    alice.write(client::encode(move, move_to(1, 110, 90).SerializeAsString()) +
                client::encode(move, move_to(2, 120, 100).SerializeAsString()));
    const auto movement = own.changes(alice);
    other.changes(bob);
    require(movement.added_size() == 0 && movement.changed_size() == 1, "movement did not coalesce to one delta");
    require(own.number(first.player_id(), 103) == 2 && other.number(first.player_id(), 103) == 2 &&
                std::get<double>(own.player(first.player_id()).fields.at(101)) == 120,
        "authoritative move missing");
    alice.quiet();
    bob.quiet();
    alice.send(move, move_to(2, 900, 500));
    error_is(alice, "stale_sequence");
    alice.send(move, move_to(3, -1, 1));
    error_is(alice, "invalid_message");
    alice.send(move, move_to(3, std::numeric_limits<double>::infinity(), 0));
    error_is(alice, "invalid_message");
    wire::heartbeat heartbeat;
    heartbeat.set_nonce(1);
    wire::heartbeat maximum;
    maximum.set_nonce(4294967295u);
    alice.write(
        client::encode(ping, heartbeat.SerializeAsString()) + client::encode(ping, maximum.SerializeAsString()));
    require(alice.receive<wire::heartbeat>(pong).nonce() == 1, "first coalesced ping lost");
    require(alice.receive<wire::heartbeat>(pong).nonce() == 4294967295u, "second coalesced ping lost");
    alice.quiet();
    bob.quiet();

    bob.send(move, move_to(1, 900, 500));
    own.changes(alice);
    other.changes(bob);
    bob.close();
    const auto removal = own.changes(alice);
    require(removal.changed_size() == 1 && removal.changed(0).removed() && removal.changed(0).fields_size() == 0,
        "disconnect did not emit a tombstone");
    require(!own.players.at(static_cast<ark::guid_t>(second.entity_id()))->state(), "retired Entity remained live");
    client fresh(port);
    fresh.write(client::encode(join, join_as("Bob").SerializeAsString()) +
                client::encode(move, move_to(1, 200, 150).SerializeAsString()));
    const auto rejoined = fresh.receive<wire::join_accepted>(accepted);
    require(rejoined.player_id() != second.player_id() && rejoined.entity_id() != second.entity_id(),
        "reconnect reused old incarnation");
    roster reset;
    reset.baseline(fresh);
    require(reset.players.size() == 2 && reset.number(rejoined.player_id(), 103) == 1,
        "new baseline did not capture pre-publication movement");
    const auto fresh_added = own.changes(alice);
    require(fresh_added.added_size() == 1 && fresh_added.changed_size() == 0,
        "new Entity must not publish an intermediate delta alongside its baseline");
    fresh.quiet();
    alice.quiet();
    fresh.close();
    own.changes(alice);
    alice.close();

    for (const auto kind : {0, 1, 2, 3, 4})
    {
        client malformed(port);
        auto bytes = client::encode(join, "").substr(0, 12);
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
            bytes[10] = 0x20;
            bytes[11] = 1;
        }
        if (kind == 3)
        {
            bytes = client::encode(join, std::string(1, static_cast<char>(0x80)));
        }
        if (kind == 4)
        {
            bytes = client::encode(12345, "");
        }
        malformed.write(bytes);
        malformed.expect_closed();
    }
    client queued(port);
    std::string overflow;
    for (unsigned index = 0; index < 9; ++index)
    {
        overflow += client::encode(ping, heartbeat.SerializeAsString());
    }
    queued.write(overflow);
    queued.expect_closed();
    client names(port);
    names.send(join, join_as(std::string(65, 'a')));
    error_is(names, "invalid_message");
    names.send(join, join_as("a\nb"));
    error_is(names, "invalid_message");
    auto old_version = join_as("Old");
    old_version.set_protocol_version(0);
    names.send(join, old_version);
    error_is(names, "invalid_message");
    names.close();
}

void exercise_attributes(unsigned short port) {
    client owner(port, true);
    owner.send(join, join_as("Attributes"));
    const auto id = owner.receive<wire::join_accepted>(accepted).player_id();
    roster own;
    own.baseline(owner);
    client observer(port);
    observer.send(join, join_as("Observer"));
    observer.receive<wire::join_accepted>(accepted);
    roster other;
    other.baseline(observer);
    own.changes(owner);
    const auto accept_action = [&](std::uint32_t sequence, wire::action_kind kind, std::uint32_t hp, std::uint32_t mp) {
        std::this_thread::sleep_for(35ms);
        owner.send(action, act(sequence, kind));
        const auto changed = own.changes(owner);
        other.changes(observer);
        require(changed.added_size() == 0 && changed.changed_size() == 1, "action sent a full/added baseline");
        for (const auto* state : {&own, &other})
        {
            require(state->number(id, 104) == hp && state->number(id, 105) == mp && state->number(id, 108) == sequence,
                "wrong authoritative attributes or acknowledgement");
        }
        return changed;
    };
    const auto reject_action = [&](std::uint32_t sequence, wire::action_kind kind, const std::string& code) {
        std::this_thread::sleep_for(35ms);
        owner.send(action, act(sequence, kind));
        error_is(owner, code, sequence);
        owner.quiet();
        observer.quiet();
    };
    accept_action(1, wire::ACTION_DAMAGE, 85, 100);
    accept_action(2, wire::ACTION_SKILL, 85, 80);
    reject_action(1, wire::ACTION_REST, "stale_sequence");
    reject_action(0, wire::ACTION_DAMAGE, "invalid_message");
    reject_action(3, static_cast<wire::action_kind>(99), "invalid_message");
    accept_action(3, wire::ACTION_REST, 100, 100);
    const auto noop = accept_action(4, wire::ACTION_REST, 100, 100);
    require(noop.changed(0).fields_size() == 1 && noop.changed(0).fields(0).index() == 108,
        "clamped action must acknowledge only action_seq");
    for (std::uint32_t next = 5; next <= 9; ++next)
    {
        accept_action(next, wire::ACTION_SKILL, 100, 100 - (next - 4) * 20);
    }
    reject_action(10, wire::ACTION_SKILL, "insufficient_resource");
    accept_action(10, wire::ACTION_REST, 100, 20);
    accept_action(11, wire::ACTION_SKILL, 100, 0);
    for (std::uint32_t next = 12; next <= 18; ++next)
    {
        accept_action(next, wire::ACTION_DAMAGE, next == 18 ? 0 : 100 - (next - 11) * 15, 0);
    }
    accept_action(19, wire::ACTION_DAMAGE, 0, 0);
    owner.send(move, move_to(1, 200, 150));
    own.changes(owner);
    other.changes(observer);
    require(own.number(id, 103) == 1 && own.number(id, 104) == 0, "zero HP blocked movement");
    accept_action(4294967295u, wire::ACTION_REST, 20, 20);
    reject_action(4294967295u, wire::ACTION_DAMAGE, "stale_sequence");
    owner.close();
    other.changes(observer);
    client fresh(port);
    fresh.send(join, join_as("Attributes"));
    const auto new_id = fresh.receive<wire::join_accepted>(accepted).player_id();
    roster reset;
    reset.baseline(fresh);
    require(new_id != id && reset.number(new_id, 104) == 100 && reset.number(new_id, 105) == 100 &&
                reset.number(new_id, 108) == 0 && reset.number(new_id, 103) == 0,
        "reconnect did not reset configured state");
    other.changes(observer);
    fresh.close();
    other.changes(observer);
    observer.close();
}

void exercise_configured_profile(unsigned short port) {
    client peer(port, true);
    peer.send(join, join_as("Configured"));
    const auto id = peer.receive<wire::join_accepted>(accepted).player_id();
    roster state;
    state.baseline(peer);
    require(state.number(id, 104) == 4294967290u && state.number(id, 105) == 4294967285u &&
                state.number(id, 106) == 4294967295u && state.number(id, 107) == 4294967295u,
        "configured defaults replaced");
    peer.send(action, act(1, wire::ACTION_REST));
    state.changes(peer);
    require(state.number(id, 104) == 4294967295u && state.number(id, 105) == 4294967295u && state.number(id, 108) == 1,
        "rest overflowed instead of clamping");
    peer.close();
}

void exercise_resource_boundary(unsigned short port) {
    client peer(port, true);
    peer.send(join, join_as("ResourceBoundary"));
    const auto id = peer.receive<wire::join_accepted>(accepted).player_id();
    roster state;
    state.baseline(peer);
    require(state.number(id, 105) == 19, "19 MP configuration missing");
    peer.send(action, act(1, wire::ACTION_SKILL));
    error_is(peer, "insufficient_resource", 1);
    peer.send(action, act(1, wire::ACTION_REST));
    state.changes(peer);
    require(state.number(id, 105) == 39 && state.number(id, 104) == 100 && state.number(id, 108) == 1,
        "rejected action mutated resources or sequence");
    peer.close();
}
} // namespace

int main(int argc, char** argv) {
    try
    {
        require(argc == 4, "usage: server-test LAUNCHER RUNTIME_DIR PORT");
        launcher process(argv[1], argv[2]);
        const auto port = static_cast<unsigned short>(std::stoul(argv[3]));
        exercise(port);
        exercise_attributes(port);
        process.stop();
        const auto path = std::filesystem::path(argv[2]) / "resource/meta/entity_schema.json";
        std::ifstream input(path);
        auto configuration = json::parse(input);
        const auto original = configuration;
        auto& profile = configuration["configs"][0]["rows"][0];
        profile["hp"] = 4294967290u;
        profile["mp"] = 4294967285u;
        profile["hp_max"] = 4294967295u;
        profile["mp_max"] = 4294967295u;
        {
            std::ofstream output(path);
            output << configuration.dump(2);
        }
        launcher configured(argv[1], argv[2], "launcher-configured-profile.log");
        exercise_configured_profile(port);
        configured.stop();
        auto boundary = original;
        boundary["configs"][0]["rows"][0]["mp"] = 19;
        {
            std::ofstream output(path);
            output << boundary.dump(2);
        }
        launcher resource_boundary(argv[1], argv[2], "launcher-resource-boundary.log");
        exercise_resource_boundary(port);
        resource_boundary.stop();
        for (const auto* field : {"hp", "move_seq", "hp_max", "mp_max"})
        {
            auto invalid = original;
            auto& invalid_profile = invalid["configs"][0]["rows"][0];
            const auto name_of_field = std::string(field);
            if (name_of_field == "hp_max" || name_of_field == "mp_max")
            {
                // Keep the current resource in bounds so only the zero maximum rejects startup.
                invalid_profile[name_of_field] = 0;
                invalid_profile[name_of_field == "hp_max" ? "hp" : "mp"] = 0;
            }
            else
            {
                invalid_profile[name_of_field] = name_of_field == "hp" ? 101 : 1;
            }
            {
                std::ofstream output(path);
                output << invalid.dump(2);
            }
            const auto name = "launcher-invalid-" + std::string(field) + ".log";
            launcher invalid_process(argv[1], argv[2], name);
            invalid_process.expect_failure(std::filesystem::path(argv[2]) / name);
        }
        {
            std::ofstream output(path);
            output << original.dump(2);
        }
        std::cout
            << "guest_room: actual launcher, Kernel entities, two peers, authoritative moves, errors, "
               "protobuf baselines/deltas/tombstones, attribute actions, configured defaults, uint32 clamp boundaries, "
               "disconnect/rejoin, invalid framing and normal shutdown passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
