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

#include <arknet/external/asio.hpp>

#include <ark/bus/msg_module.hpp>
#include <ark/net/net_service_manager_module.hpp>
#include <ark/plugin_manager.hpp>
#include <ark/time/date_time.hpp>

namespace {
using namespace std::chrono_literals;
const ark::bus_addr client_id(1, 1, 31, 1);
const ark::bus_addr server_id(1, 1, 30, 1);
volatile std::sig_atomic_t stopping = 0;

void on_signal(int) {
    stopping = 1;
}

void require(bool condition, const std::string& message) {
    if (!condition)
    {
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
            if (replies == 0)
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
            mark("second-roundtrip");
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

int peer(const std::string& role, const std::string& plugins, int generation) {
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
        started = manager.start(ark::date_time::get_now_time(), plugins + "/", "",
            {{"ark_bus_plugin", "bus.toml"}, {"ark_net_plugin", ""}, {"ark_utility_plugin", ""}});
    }
    catch (const std::exception& error)
    {
        if (role != "invalid")
        {
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
        require(manager.stop(ark::date_time::get_now_time()), "Invalid static target cleanup failed");
        return 0;
    }
    require(started, "Actual plugin startup failed: " + manager.get_startup_failure());
    auto* network = manager.find_module<ark::net_service_manager_module>();
    auto* messages = manager.find_module<ark::msg_module>();
    require(network && messages, "Actual Bus/Net interfaces missing");
    receiver received{manager, *network, *messages, generation};
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
    require(manager.stop(ark::date_time::get_now_time()) && manager.stop(ark::date_time::get_now_time()),
        "Actual plugin normal repeated stop failed");
    return 0;
}

class child
{
public:
    child(const std::string& executable, const std::string& role, const std::string& plugins, int generation,
        const std::string& log_name) {
        const auto generation_text = std::to_string(generation);
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
            ::execl(executable.c_str(), executable.c_str(), role.c_str(), plugins.c_str(), generation_text.c_str(),
                nullptr);
            ::_exit(127);
        }
    }

    child(const child&) = delete;
    child& operator=(const child&) = delete;

    ~child() {
        if (pid_ > 0)
        {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, &status_, 0);
        }
    }

    bool exited() {
        if (pid_ > 0)
        {
            const auto result = ::waitpid(pid_, &status_, WNOHANG);
            require(result >= 0, "waitpid failed");
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
        const auto deadline = std::chrono::steady_clock::now() + 5s;
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
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!std::filesystem::exists(name) && std::chrono::steady_clock::now() < deadline)
    {
        require(!process.exited(), "Owned child exited before " + name);
        std::this_thread::sleep_for(5ms);
    }
    require(std::filesystem::exists(name), "Timed out waiting for " + name);
}

int controller(const std::string& executable, const std::string& plugins, const std::string& runtime) {
    const auto directory = std::filesystem::path(runtime) /
                           (std::to_string(::getpid()) + "-" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    std::cout << "Static Bus raw child logs: " << directory << std::endl;
    int invalid_case = 0;
    for (const auto& endpoint : {"", "tcp://0.0.0.0:1234", "tcp://[::]:1234", "tcp://[0:0:0:0:0:0:0:0]:1234",
             "tcp://127.0.0.1:0", "ws://127.0.0.1:1234", "tcp://127.0.0.1:1234/path"})
    {
        write_configuration(endpoint);
        child invalid(executable, "invalid", plugins, 0, "invalid-" + std::to_string(++invalid_case) + ".log");
        invalid.finish();
    }
    asio::io_context context;
    asio::ip::tcp::acceptor reservation(context, {asio::ip::make_address("127.0.0.1"), 0});
    const auto port = reservation.local_endpoint().port();
    reservation.close();
    write_configuration("tcp://127.0.0.1:" + std::to_string(port));
    child first(executable, "server", plugins, 1, "server-1.log");
    await_marker("server-ready-1", first, 5s);
    child client(executable, "client", plugins, 0, "client.log");
    await_marker("first-roundtrip", client, 5s);
    first.stop();
    await_marker("disconnected", client, 5s);
    child second(executable, "server", plugins, 2, "server-2.log");
    await_marker("server-ready-2", second, 5s);
    await_marker("second-roundtrip", client, 35s);
    client.finish();
    second.stop();
    std::cout << "Static Bus actual two-process DSOs: eligibility, invalid endpoint diagnostics, SS registration, "
                 "Protobuf request/response, lost-route rejection, target restart, 30s policy reconnect and normal "
                 "stop passed\n";
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
        require(argc == 4, "Expected plugin directory and runtime, or peer arguments");
        return peer(argv[1], argv[2], std::stoi(argv[3]));
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
