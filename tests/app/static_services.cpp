// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

#include <arknet/external/asio.hpp>

#include <ark/bus/msg_module.hpp>
#include <ark/net/net_service_manager_module.hpp>
#include <ark/plugin_manager.hpp>
#include <ark/time/date_time.hpp>
#include "owner_drain.hpp"

#ifdef ARK_COMPOSED_PLUGIN_TEST
#include <array>
#include <atomic>
#include <memory>

#include <arknet/external/beast.hpp>

#include <ark/http/http_server_module.hpp>
#include <ark/kernel/data_list_impl.hpp>
#include <ark/kernel/kernel_module.hpp>
#include <ark/kernel/map_module.hpp>
#endif

namespace {
using namespace std::chrono_literals;
const ark::bus_addr client_id(1, 1, 31, 1);
const ark::bus_addr server_id(1, 1, 30, 1);
volatile std::sig_atomic_t stopping = 0;
auto controller_deadline = std::chrono::steady_clock::time_point::max();
#ifdef ARK_COMPOSED_PLUGIN_TEST
bool composed_peer{};
#endif

std::chrono::steady_clock::time_point bounded_deadline(std::chrono::seconds duration) {
    return std::min(controller_deadline, std::chrono::steady_clock::now() + duration);
}

void on_signal(int) {
    stopping = 1;
}

void require(bool condition, const std::string& message) {
    if (!condition)
    {
#ifdef ARK_COMPOSED_PLUGIN_TEST
        if (composed_peer)
        {
            std::cerr << message << std::endl;
            std::_Exit(1);
        }
#endif
        throw std::runtime_error(message);
    }
}

void mark(const std::string& name) {
    std::ofstream output(name);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << "ready\n";
}

void write_configuration(const std::string& endpoint) {
    std::ofstream output("bus.toml");
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << "[bus]\n"
              "[[bus.applications]]\nname = 'game'\ntype = 31\n"
              "[[bus.applications]]\nname = 'world'\ntype = 30\n"
              "[[bus.applications]]\nname = 'login'\ntype = 32\n"
              "[[bus.relations]]\nsource = 'game'\ntarget = 'world'\n"
              "[[bus.relations]]\nsource = 'game'\ntarget = 'game'\n";
    for (const auto& id : {"1.1.31.1", "1.1.30.1", "1.2.30.1", "2.1.30.1", "1.1.32.1"})
    {
        output << "[[bus.processes]]\nbus_id = '" << id << "'\nserver_endpoint = ''\nintranet_endpoint = '"
               << (std::string(id) == "1.1.30.1" ? endpoint : "") << "'\nthreads = 1\nmax_connections = 10\n";
    }
    output << "[bus.registry]\nhost = '127.0.0.1'\nport = 0\nservice_name = 'static-test'\n"
              "check_interval = '10s'\ncheck_timeout = '3s'\n";
}

#ifdef ARK_COMPOSED_PLUGIN_TEST
// No Kernel/Net object crosses onto an HTTP worker. Only this independently
// owned capture is retained by the native route, and snapshots own their values.
struct route_capture
{
    std::thread::id owner;
    std::atomic<bool>& retired;
    std::atomic<unsigned> calls{};
    std::atomic<bool> worker{};

    ~route_capture() {
        retired.store(true);
    }
};

void require_refused(unsigned short port) {
    asio::io_context context;
    beast::tcp_stream stream(context);
    stream.expires_at(bounded_deadline(2s));
    arknet::error_code result;
    stream.async_connect(
        {asio::ip::make_address("127.0.0.1"), port}, [&](arknet::error_code error) { result = error; });
    context.run();
    require(result == asio::error::connection_refused, "Closed composed endpoint accepted a fresh connection");
}

void binary_echo(unsigned short port) {
    asio::io_context context;
    beast::tcp_stream stream(context);
    beast::flat_buffer buffer;
    arknet::http::request<arknet::http::string_body> request(arknet::http::verb::post, "/echo", 11);
    request.set(arknet::http::field::host, "127.0.0.1");
    request.body() = std::string("route\0body", 10);
    request.keep_alive(false);
    request.prepare_payload();
    arknet::http::response<arknet::http::string_body> response;
    arknet::error_code result;
    stream.expires_at(bounded_deadline(2s));
    stream.async_connect({asio::ip::make_address("127.0.0.1"), port}, [&](arknet::error_code error) {
        if (error)
        {
            result = error;
            return;
        }
        stream.expires_at(bounded_deadline(2s));
        arknet::http::async_write(stream, request, [&](arknet::error_code error, std::size_t) {
            if (error)
            {
                result = error;
                return;
            }
            stream.expires_at(bounded_deadline(2s));
            arknet::http::async_read(
                stream, buffer, response, [&](arknet::error_code error, std::size_t) { result = error; });
        });
    });
    context.run();
    require(!result && response.result() == arknet::http::status::ok && response.body() == request.body(),
        "Composed HTTP binary echo did not complete while SS was live");
}

class composition_probe
{
public:
    composition_probe(ark::plugin_manager& manager, unsigned short port)
        : manager_(manager)
        , port_(port) {
        auto* maps = manager_.find_module<ark::map_module>();
        kernel_ = manager_.find_module<ark::kernel_module>();
        auto* http = manager_.find_module<ark::http_server_module>();
        require(maps && kernel_ && http && maps->create_map(1), "Composed Kernel/HTTP providers or map missing");
        player_ = kernel_->create_entity(ark::null_guid, 1, 0, "GuestPlayer", 1, ark::data_list_impl());
        require(player_ && player_->get_u_int32(104) == 100 && player_->set_u_int32(104, 85) &&
                    player_->get_u_int32(104) == 85,
            "Composed GuestPlayer JSON profile/mutation changed");
        snapshot_ = kernel_->capture_scalar_snapshot(*player_, std::array<std::uint32_t, 1>{104});
        require(std::get<std::uint32_t>(snapshot_.fields.at(104)) == 85, "Composed snapshot lost owned HP value");
        state_ = std::make_shared<route_capture>(std::this_thread::get_id(), retired_);
        weak_ = state_;
        http->get_router().add(arknet::http::verb::post, "/echo", [state = state_](const auto& route, auto& response) {
            ++state->calls;
            state->worker.store(std::this_thread::get_id() != state->owner);
            response.body() = route.request.body();
        });
        require(http->start_server(), "Composed HTTP listener did not start");
        std::cout << "Composed Kernel profile 100 -> 85 and owning snapshot passed on owner thread\n";
    }

    ~composition_probe() {
        if (std::uncaught_exceptions())
        {
            std::cerr << "Composed peer failed; retain live callback/DSO owners until isolated exit" << std::endl;
            std::_Exit(1);
        }
    }

    void before_stop() {
        require(state_->calls.load() > 0 && state_->worker.load(), "Composed echo did not execute on HTTP worker");
        require(player_->get_u_int32(104) == 85 && kernel_->destroy_entity(player_->get_id()),
            "Composed Kernel owner-thread entity retirement failed");
        player_.reset();
        state_.reset();
        // The route alone now owns its capture until the real HTTP module closes.
        require(!weak_.expired(), "Composed route released its capture before native closure");
    }

    void after_stop() {
        require(weak_.expired() && retired_.load(), "Composed native route retained its owner after normal stop");
        require(snapshot_.class_name == "GuestPlayer" && std::get<std::uint32_t>(snapshot_.fields.at(104)) == 85,
            "Composed owning snapshot did not survive Entity/module destruction");
        require_refused(port_);
        std::cout << "Composed Entity/capture retirement, post-stop owning snapshot and HTTP endpoint closure passed\n";
    }

private:
    ark::plugin_manager& manager_;
    unsigned short port_;
    ark::kernel_module* kernel_{};
    std::shared_ptr<ark::entity> player_;
    ark::entity_sync::scalar_snapshot snapshot_;
    std::atomic<bool> retired_{};
    std::shared_ptr<route_capture> state_;
    std::weak_ptr<route_capture> weak_;
};
#endif

struct receiver
{
    ark::plugin_manager& manager;
    ark::net_service_manager_module& network;
    ark::msg_module& messages;
    int generation;
    const std::thread::id owner = std::this_thread::get_id();
    int replies{};
    int connects{};
    int disconnects{};
    bool defer_markers{};

    void request(const ark::net_msg* message, ark::conv_id_t session) {
        require(std::this_thread::get_id() == owner, "SS request callback left manager owner thread");
        require(message->get_src_bus() == client_id.bus_id && message->get_dst_bus() == server_id.bus_id &&
                    message->get_actor_id() == ark::guid_t{42},
            "SS request addressing changed");
        require(session != 0 && network.get_session_id(client_id.bus_id) == session &&
                    network.get_net_connection_bus(server_id.bus_id, client_id.bus_id) != nullptr,
            "SS registration did not establish server route and session before request");
        ark::protobuf::point_3d body;
        require(body.ParseFromArray(message->get_msg_data(), static_cast<int>(message->get_msg_length())) &&
                    body.y() == 12.5F && body.z() == -7.0F,
            "SS request Protobuf payload changed");
        body.set_x(static_cast<float>(generation));
        require(messages.send_to_intra(session, 45011, body, ark::guid_t{42}), "SS response rejected");
        ++replies;
    }

    void response(const ark::net_msg* message, ark::conv_id_t) {
        require(std::this_thread::get_id() == owner, "SS response callback left manager owner thread");
        require(message->get_src_bus() == server_id.bus_id && message->get_actor_id() == ark::guid_t{42},
            "SS response addressing changed");
        ark::protobuf::point_3d body;
        require(body.ParseFromArray(message->get_msg_data(), static_cast<int>(message->get_msg_length())) &&
                    body.y() == 12.5F && body.z() == -7.0F,
            "SS response Protobuf payload changed");
        if (body.x() == 1.0F)
        {
            if (replies == 0 && !defer_markers)
            {
                mark("first-roundtrip");
            }
            replies = 1;
        }
        else
        {
            require(body.x() == 2.0F && replies == 1 && disconnects == 1 && connects == 2,
                "Restart response arrived without exactly one disconnect and reconnect");
            replies = 2;
            if (!defer_markers)
            {
                mark("second-roundtrip");
            }
        }
    }

    void event(const ark::net_event* event) {
        require(std::this_thread::get_id() == owner, "SS connection event left manager owner thread");
        require(event->get_bus_id() == server_id.bus_id, "Unexpected static target connection event");
        if (event->get_type() == ark::net_event_type::connect)
        {
            ++connects;
        }
        else if (event->get_type() == ark::net_event_type::disconnect)
        {
            ++disconnects;
            ark::protobuf::point_3d body;
            require(replies == 1 && !network.get_net_connection_bus(client_id.bus_id, server_id.bus_id) &&
                        !messages.send_msg_by_bus_id(server_id.bus_id, 45010, body),
                "Disconnected target retained a usable route");
            mark("disconnected");
        }
    }
};

int peer(const std::string& role, const std::string& plugins, int generation, unsigned short http_port = 0,
    unsigned short remote_http_port = 0) {
    std::signal(SIGTERM, on_signal);
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& text) { std::cout << text << std::endl; });
    ark::plugin_manager manager;
    manager.set_bus_id(role == "server" ? server_id.bus_id : client_id.bus_id);
    bool started = false;
    std::string diagnostic;
    try
    {
        std::unordered_map<std::string, std::string> selected{
            {"ark_bus_plugin", "bus.toml"}, {"ark_net_plugin", ""}, {"ark_utility_plugin", ""}};
#ifdef ARK_COMPOSED_PLUGIN_TEST
        if (http_port)
        {
            selected.emplace("ark_kernel_plugin", "");
            selected.emplace("ark_http_plugin", role + "-http.toml");
        }
#else
        require(http_port == 0 && remote_http_port == 0, "Composition needs the independent composed test target");
#endif
        started = manager.start(ark::date_time::get_now_time(), plugins + "/", "", selected);
    }
    catch (const std::exception& error)
    {
        if (role != "invalid")
        {
#ifdef ARK_COMPOSED_PLUGIN_TEST
            if (http_port)
            {
                std::cerr << "Composed startup failed: " << error.what() << std::endl;
                std::_Exit(1);
            }
#endif
            throw;
        }
        diagnostic = error.what();
    }
    if (role == "invalid")
    {
        require(!started && manager.get_startup_failure().find("post_init:") != std::string::npos &&
                    diagnostic.find("static Bus target 1.1.30.1") != std::string::npos &&
                    diagnostic.find("intranet_endpoint") != std::string::npos,
            "Invalid static target lacked a specific startup diagnostic: " + diagnostic);
        // Fixture hygiene only: every DSO/owner remains loaded after failed
        // start, and this child exits immediately; no production rollback or
        // same-process retry contract is being exercised.
        ark::test::drain_owner(manager, ark::date_time::get_now_time, std::chrono::steady_clock::now() + 5s,
            "Invalid static target fixture cleanup");
        return 0;
    }
    require(started, "Actual plugin startup failed: " + manager.get_startup_failure());
    auto* network = manager.find_module<ark::net_service_manager_module>();
    auto* messages = manager.find_module<ark::msg_module>();
    require(network && messages, "Actual Bus/Net interfaces missing");
#ifdef ARK_COMPOSED_PLUGIN_TEST
    std::unique_ptr<composition_probe> composed;
    if (http_port)
    {
        composed = std::make_unique<composition_probe>(manager, http_port);
    }
    int http_generation{};
#endif
    receiver received{manager, *network, *messages, generation};
    received.defer_markers = http_port != 0;
    auto clients = network->get_client_service(ark::app_type::ark_app_world);
    if (role == "server")
    {
        require(!clients, "Undeclared outgoing relation created a server client");
        require(network->create_server(), "Actual SS listener failed to start");
        auto server = network->get_intra_net_server();
        require(server && !network->get_inter_net_server(), "Static target listener selection changed");
        require(server->reg_msg_callback(45010, &received, &receiver::request), "SS request callback rejected");
        mark("server-ready-" + std::to_string(generation));
    }
    else
    {
        require(clients && !network->get_client_service(ark::app_type::ark_app_game) &&
                    !network->get_client_service(ark::app_type::ark_app_login),
            "Static bootstrap did not exclude self or undeclared type");
        require(clients->reg_msg_callback(45011, &received, &receiver::response) &&
                    clients->reg_net_event_callback(&received, &receiver::event),
            "SS client callbacks rejected");
    }
    const auto started_at = std::chrono::steady_clock::now();
    auto send_at = started_at;
    // Existing reconnect interval is 30s. Allow 5s for the initial and final
    // round trips; do not change or accelerate the production retry clock.
    const auto deadline = started_at + 40s;
    while (!stopping && std::chrono::steady_clock::now() < deadline && (role == "server" || received.replies != 2))
    {
        require(manager.update(ark::date_time::get_now_time()), "Actual plugin update failed");
#ifdef ARK_COMPOSED_PLUGIN_TEST
        if (composed && role == "client" && received.replies > http_generation)
        {
            binary_echo(http_port);
            binary_echo(remote_http_port);
            http_generation = received.replies;
            std::cout << "Composed binary HTTP echo on both peers while SS generation " << http_generation
                      << " live passed" << std::endl;
            mark(http_generation == 1 ? "first-roundtrip" : "second-roundtrip");
        }
#endif
        if (role == "client")
        {
            require(!clients->get_connection_info(ark::bus_addr(1, 2, 30, 1).bus_id) &&
                        !clients->get_connection_info(ark::bus_addr(2, 1, 30, 1).bus_id),
                "Bootstrap crossed configured channel/zone boundary");
            const auto now = std::chrono::steady_clock::now();
            if (now >= send_at && network->get_net_connection_bus(client_id.bus_id, server_id.bus_id))
            {
                ark::protobuf::point_3d body;
                body.set_y(12.5F);
                body.set_z(-7.0F);
                require(messages->send_msg_by_bus_id(server_id.bus_id, 45010, body, ark::guid_t{42}),
                    "Connected static Bus request rejected");
                send_at = now + 100ms;
            }
        }
        std::this_thread::sleep_for(1ms);
    }
    require(role == "server" ? stopping != 0 : received.replies == 2, "Bounded SS peer acceptance timed out");
    // Drop public shared services while their DSO implementations remain loaded.
    clients.reset();
#ifdef ARK_COMPOSED_PLUGIN_TEST
    if (composed)
    {
        composed->before_stop();
    }
#endif
    ark::test::drain_owner(
        manager, ark::date_time::get_now_time, std::chrono::steady_clock::now() + 5s, "Static Bus actual DSOs");
    require(manager.stop(ark::date_time::get_now_time()), "Actual plugin normal repeated stop failed");
#ifdef ARK_COMPOSED_PLUGIN_TEST
    if (composed)
    {
        composed->after_stop();
    }
#endif
    return 0;
}

class child
{
public:
    child(const std::string& executable, const std::string& role, const std::string& plugins, int generation,
        const std::string& log_name, unsigned short http_port = 0, unsigned short remote_http_port = 0) {
        const auto generation_text = std::to_string(generation);
        const auto http_text = std::to_string(http_port);
        const auto remote_http_text = std::to_string(remote_http_port);
        pid_ = ::fork();
        require(pid_ >= 0, "fork failed");
        if (pid_ == 0)
        {
            const auto log = ::open(log_name.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
            if (log < 0 || ::dup2(log, STDOUT_FILENO) < 0 || ::dup2(log, STDERR_FILENO) < 0)
            {
                ::_exit(126);
            }
            ::close(log);
            if (http_port)
            {
                ::execl(executable.c_str(), executable.c_str(), role.c_str(), plugins.c_str(), generation_text.c_str(),
                    http_text.c_str(), remote_http_text.c_str(), static_cast<char*>(nullptr));
            }
            else
            {
                ::execl(executable.c_str(), executable.c_str(), role.c_str(), plugins.c_str(), generation_text.c_str(),
                    static_cast<char*>(nullptr));
            }
            ::_exit(127);
        }
    }

    child(const child&) = delete;
    child& operator=(const child&) = delete;

    ~child() {
        if (pid_ > 0)
        {
            ::kill(pid_, SIGKILL);
            while (::waitpid(pid_, &status_, 0) < 0 && errno == EINTR)
            {
            }
        }
    }

    bool exited() {
        if (pid_ > 0)
        {
            const auto result = ::waitpid(pid_, &status_, WNOHANG);
            require(result >= 0 || errno == EINTR, "waitpid failed");
            if (result == pid_)
            {
                pid_ = -1;
            }
        }
        return pid_ < 0;
    }

    void stop() {
        require(pid_ > 0 && ::kill(pid_, SIGTERM) == 0, "Could not stop owned server child");
        finish();
    }

    void finish() {
        const auto deadline = bounded_deadline(5s);
        while (!exited() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(5ms);
        }
        require(exited() && WIFEXITED(status_) && WEXITSTATUS(status_) == 0, "Owned child did not exit normally");
    }

private:
    pid_t pid_{-1};
    int status_{};
};

void await_marker(const std::string& name, child& process, std::chrono::seconds timeout) {
    const auto deadline = bounded_deadline(timeout);
    while (!std::filesystem::exists(name) && std::chrono::steady_clock::now() < deadline)
    {
        require(!process.exited(), "Owned child exited before " + name);
        std::this_thread::sleep_for(5ms);
    }
    require(std::filesystem::exists(name), "Timed out waiting for " + name);
}

int controller(const std::string& executable, const std::string& plugins, const std::string& runtime,
    const std::string& schema = {}) {
    const auto directory = std::filesystem::path(runtime) /
                           (std::to_string(::getpid()) + "-" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    std::cout << "Static Bus raw child logs: " << directory << std::endl;
    if (!schema.empty())
    {
        controller_deadline = std::chrono::steady_clock::now() + 55s;
    }
    int invalid_case = 0;
    if (schema.empty())
    {
        for (const auto& endpoint : {"", "tcp://0.0.0.0:1234", "tcp://[::]:1234", "tcp://[0:0:0:0:0:0:0:0]:1234",
                 "tcp://127.0.0.1:0", "ws://127.0.0.1:1234", "tcp://127.0.0.1:1234/path"})
        {
            write_configuration(endpoint);
            child invalid(executable, "invalid", plugins, 0, "invalid-" + std::to_string(++invalid_case) + ".log");
            invalid.finish();
        }
    }
    asio::io_context context;
    asio::ip::tcp::acceptor reservation(context, {asio::ip::make_address("127.0.0.1"), 0});
    const auto port = reservation.local_endpoint().port();
    write_configuration("tcp://127.0.0.1:" + std::to_string(port));
    unsigned short server_http{}, client_http{};
#ifdef ARK_COMPOSED_PLUGIN_TEST
    if (!schema.empty())
    {
        std::filesystem::create_directories("resource/meta");
        std::filesystem::copy_file(schema, "resource/meta/entity_schema.json");
        asio::ip::tcp::acceptor server_reservation(context, {asio::ip::make_address("127.0.0.1"), 0});
        asio::ip::tcp::acceptor client_reservation(context, {asio::ip::make_address("127.0.0.1"), 0});
        server_http = server_reservation.local_endpoint().port();
        client_http = client_reservation.local_endpoint().port();
        for (const auto& entry : {std::pair{"server", server_http}, std::pair{"client", client_http}})
        {
            std::ofstream http(std::string(entry.first) + "-http.toml");
            http.exceptions(std::ios::failbit | std::ios::badbit);
            http << "[http]\nhost = '127.0.0.1'\nport = " << entry.second << "\nthreads = 3\n";
        }
    }
#endif
    reservation.close();
    child first(executable, "server", plugins, 1, "server-1.log", server_http);
    await_marker("server-ready-1", first, 5s);
    child client(executable, "client", plugins, 0, "client.log", client_http, server_http);
    await_marker("first-roundtrip", client, 5s);
    first.stop();
#ifdef ARK_COMPOSED_PLUGIN_TEST
    if (server_http)
    {
        require_refused(server_http);
        require_refused(port);
    }
#endif
    await_marker("disconnected", client, 5s);
    child second(executable, "server", plugins, 2, "server-2.log", server_http);
    await_marker("server-ready-2", second, 5s);
    await_marker("second-roundtrip", client, 35s);
    client.finish();
    second.stop();
#ifdef ARK_COMPOSED_PLUGIN_TEST
    if (server_http)
    {
        require_refused(server_http);
        require_refused(client_http);
        require_refused(port);
        std::cout
            << "Composed actual Bus/Net/Utility/Kernel/HTTP DSOs: both generations, binary HTTP, JSON Entity mutation, "
               "owned snapshots, callback retirement, endpoint closure and repeated normal stop passed\n";
    }
#endif
    if (schema.empty())
    {
        std::cout << "Static Bus actual two-process DSOs: eligibility, invalid endpoint diagnostics, SS registration, "
                     "Protobuf request/response, lost-route rejection, target restart, 30s policy reconnect and normal "
                     "stop passed\n";
    }
    return 0;
}
} // namespace

int main(int argc, char* argv[]) {
    try
    {
        if (argc == 3)
        {
            return controller(
                std::filesystem::absolute(argv[0]).string(), std::filesystem::absolute(argv[1]).string(), argv[2]);
        }
#ifdef ARK_COMPOSED_PLUGIN_TEST
        if (argc == 5 && std::string(argv[3]) == "--composed")
        {
            return controller(std::filesystem::absolute(argv[0]).string(), std::filesystem::absolute(argv[1]).string(),
                argv[2], std::filesystem::absolute(argv[4]).string());
        }
        if (argc == 6)
        {
            composed_peer = true;
            // The composed probe prevents unwinding active callback captures;
            // controller timeouts still terminate/reap only this owned child.
            try
            {
                return peer(argv[1], argv[2], std::stoi(argv[3]), static_cast<unsigned short>(std::stoi(argv[4])),
                    static_cast<unsigned short>(std::stoi(argv[5])));
            }
            catch (const std::exception& error)
            {
                std::cerr << error.what() << std::endl;
                std::_Exit(1);
            }
        }
#endif
        require(argc == 4, "Expected plugin directory and runtime, or peer arguments");
        return peer(argv[1], argv[2], std::stoi(argv[3]));
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
