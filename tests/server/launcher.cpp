// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct socket_owner
{
    int value = ::socket(AF_INET, SOCK_STREAM, 0);

    ~socket_owner() {
        if (value >= 0)
        {
            ::close(value);
        }
    }
};

struct child_owner
{
    pid_t pid = -1;

    ~child_owner() {
        if (pid > 0)
        {
            // Only this helper's unreaped child is eligible for cleanup.
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR)
            {
            }
        }
    }
};

sockaddr_in address(unsigned short port) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    result.sin_port = htons(port);
    return result;
}

int connect_to(int socket, unsigned short port) {
    require(socket >= 0, "socket creation failed");
    require(::fcntl(socket, F_SETFL, O_NONBLOCK) == 0, "nonblocking socket setup failed");
    const auto endpoint = address(port);
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == 0)
    {
        return 0;
    }
    if (errno != EINPROGRESS)
    {
        return errno;
    }
    pollfd pending{socket, POLLOUT, 0};
    const auto ready = ::poll(&pending, 1, 100);
    require(ready >= 0, "connection poll failed");
    if (ready == 0)
    {
        return ETIMEDOUT;
    }
    int error = 0;
    socklen_t size = sizeof(error);
    require(::getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &size) == 0, "connection result failed");
    return error;
}

std::string read_log(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace

int main(int argc, char** argv) {
    std::filesystem::path child_log;
    int observed_status = -1;
    try
    {
        require(argc == 5 || argc == 7,
            "Expected launcher, plugin directory, source root, fresh runtime directory and optional role/scenario");
        const std::string role = argc == 7 ? argv[5] : "login";
        const std::string scenario = argc == 7 ? argv[6] : "healthy";
        require(role == "login" || role == "proxy" || role == "router" || role == "master" || role == "world" ||
                    role == "dir" || role == "game",
            "Unsupported server startup role");
        require(scenario == "healthy" || scenario == "occupied", "Unknown startup scenario");
        const bool with_kernel = role == "game";
        const auto application_name = with_kernel ? "game" : "login";
        const auto application_type = with_kernel ? 31 : 32;
        const auto bus_id = "1.1." + std::to_string(application_type) + ".1";
        const auto bus_argument = "--busid=" + bus_id;
        const auto name_argument = std::string("--name=") + application_name;
        const auto program = std::filesystem::absolute(argv[1]).string();
        const auto plugins = std::filesystem::absolute(argv[2]).string();
        const auto source = std::filesystem::absolute(argv[3]);
        const auto runtime = std::filesystem::absolute(argv[4]);
        require(std::filesystem::create_directory(runtime), "Runtime directory must be fresh");
        std::filesystem::permissions(runtime, std::filesystem::perms::owner_all);
        const auto log_path = runtime / "output.log";
        child_log = log_path;
        unsigned short port = 0;
        socket_owner reservation;
        {
            const auto endpoint = address(0);
            require(reservation.value >= 0 &&
                        ::bind(reservation.value, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == 0,
                "Ephemeral loopback port allocation failed");
            sockaddr_in allocated{};
            socklen_t size = sizeof(allocated);
            require(::getsockname(reservation.value, reinterpret_cast<sockaddr*>(&allocated), &size) == 0,
                "Ephemeral loopback port lookup failed");
            port = ntohs(allocated.sin_port);
        }
        if (scenario == "occupied")
        {
            require(::listen(reservation.value, 1) == 0, "Owned collision listener could not start");
        }
        else
        {
            ::close(reservation.value);
            reservation.value = -1;
        }
        // Healthy startup releases the ephemeral reservation before the launcher
        // binds. The collision scenario retains only this helper's own listener.
        if (with_kernel)
        {
            std::filesystem::create_directories(runtime / "resource/meta");
            std::ofstream schema(runtime / "resource/meta/entity_schema.json");
            schema.exceptions(std::ios::failbit | std::ios::badbit);
            // Game registers callbacks for Player during startup; no entities or
            // business operations are created by this listener lifecycle test.
            schema << R"({"format":"ark.entity-schema","version":1,"classes":[
{"name":"Player","fields":[],"tables":[],"containers":[]}],"configs":[]})";
            schema.close();
        }
        std::ofstream manifest(runtime / "application.toml");
        manifest << "[application]\nplugin_library_dir = '" << plugins << "/'\nplugin_config_dir = './'\n"
                 << "[[plugins]]\nname = 'ark_utility_plugin'\nconfig = ''\n"
                 << "[[plugins]]\nname = 'ark_bus_plugin'\nconfig = 'bus.toml'\n"
                 << "[[plugins]]\nname = 'ark_net_plugin'\nconfig = ''\n";
        if (with_kernel)
        {
            manifest << "[[plugins]]\nname = 'ark_kernel_plugin'\nconfig = ''\n";
        }
        manifest << "[[plugins]]\nname = 'ark_" << role << "_plugin'\nconfig = ''\n";
        manifest.close();
        std::ofstream bus(runtime / "bus.toml");
        bus << "[bus]\nrelations = []\n[[bus.applications]]\nname = '" << application_name
            << "'\ntype = " << application_type << "\n[[bus.processes]]\nbus_id = '" << bus_id
            << "'\nserver_endpoint = 'tcp://127.0.0.1:" << port
            << "'\nintranet_endpoint = ''\nmax_connections = 8\nthreads = 1\n"
            << "[bus.registry]\nhost = '127.0.0.1'\nport = 0\nservice_name = 'unused'\n"
            << "check_interval = '10s'\ncheck_timeout = '3s'\n";
        bus.close();
        const auto app_conf = "--app_conf=" + (runtime / "application.toml").string();
        child_owner child;
        child.pid = ::fork();
        require(child.pid >= 0, "fork failed");
        if (child.pid == 0)
        {
            if (::chdir(runtime.c_str()) != 0)
            {
                std::_Exit(125);
            }
            const auto output = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (output < 0 || ::dup2(output, STDOUT_FILENO) < 0 || ::dup2(output, STDERR_FILENO) < 0)
            {
                std::_Exit(125);
            }
            ::close(output);
            ::execl(program.c_str(), program.c_str(), bus_argument.c_str(), name_argument.c_str(), app_conf.c_str(),
                static_cast<char*>(nullptr));
            std::_Exit(126);
        }
        const auto startup_deadline = std::chrono::steady_clock::now() + 10s;
        for (;;)
        {
            int status = 0;
            const auto waited = ::waitpid(child.pid, &status, WNOHANG);
            if (waited == child.pid)
            {
                observed_status = status;
                child.pid = -1;
                require(scenario == "occupied", "Launcher exited before readiness");
                require(WIFEXITED(status) && WEXITSTATUS(status) == 1,
                    "Listener startup failure did not produce launcher exit 1");
                const auto output = read_log(log_path);
                const auto failure_begin = output.find("Application startup failed at post_init: ");
                require(failure_begin != std::string::npos, "Launcher did not emit a complete startup failure line");
                const auto failure_end = output.find('\n', failure_begin);
                const auto failure_line = output.substr(failure_begin, failure_end - failure_begin);
                require(failure_line.find(role + "_net_module") != std::string::npos &&
                            failure_line.find(": returned false") != std::string::npos,
                    "Launcher lost the failing role and lifecycle phase");
                require(output.find("Start net server successful") == std::string::npos,
                    "Occupied endpoint was reported as a healthy listener");
                std::cout << role << ": actual DSOs rejected an owned occupied endpoint with role/phase and exit 1\n";
                return 0;
            }
            require(waited == 0, "Launcher startup wait failed");
            if (read_log(log_path).find("Start net server successful") != std::string::npos)
            {
                require(scenario == "healthy", "Role accepted an occupied endpoint");
                break;
            }
            require(std::chrono::steady_clock::now() < startup_deadline, "Launcher startup timed out");
            std::this_thread::sleep_for(10ms);
        }
        socket_owner peer;
        require(connect_to(peer.value, port) == 0, "Real role loopback listener did not accept a connection");
        require(::kill(child.pid, SIGTERM) == 0, "Owned launcher signal failed");
        const auto stop_deadline = std::chrono::steady_clock::now() + 5s;
        pollfd closing{peer.value, POLLIN, 0};
        require(::poll(&closing, 1, 5000) > 0, "Stopping launcher did not close its send side");
        char byte = 0;
        const auto received = ::recv(peer.value, &byte, 1, 0);
        require(received == 0 || (received < 0 && errno == ECONNRESET), "Stopped launcher retained its connected peer");
        // Cooperate with the transport's graceful half-close. Waiting for the
        // process before replying to its FIN would wait for its native deadline.
        ::shutdown(peer.value, SHUT_WR);
        int status = 0;
        for (;;)
        {
            const auto waited = ::waitpid(child.pid, &status, WNOHANG);
            require(waited >= 0, "Launcher wait failed");
            if (waited == child.pid)
            {
                child.pid = -1;
                break;
            }
            require(std::chrono::steady_clock::now() < stop_deadline, "Launcher normal stop timed out");
            std::this_thread::sleep_for(10ms);
        }
        require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Launcher did not exit successfully");
        socket_owner fresh;
        require(connect_to(fresh.value, port) == ECONNREFUSED, "Stopped launcher retained its listener");
        const auto output = read_log(log_path);
        require(output.find("ARK Server stop ...") != std::string::npos, "Normal stop diagnostic is missing");
        require(output.find("Application startup failed") == std::string::npos &&
                    output.find("Shutdown failed") == std::string::npos,
            "Launcher reported a lifecycle failure");
        std::cout << role
                  << (with_kernel ? ": actual Utility/Bus/Net/Kernel/Game DSOs: "
                                  : ": actual Utility/Bus/Net/role DSOs: ")
                  << "loopback connection, signal shutdown, peer closure, "
                     "listener refusal and exit 0 passed; no business handler claimed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        if (observed_status >= 0)
        {
            if (WIFSIGNALED(observed_status))
            {
                std::cerr << "Child terminated by signal " << WTERMSIG(observed_status) << '\n';
            }
            else if (WIFEXITED(observed_status))
            {
                std::cerr << "Child exit code " << WEXITSTATUS(observed_status) << '\n';
            }
        }
        std::ifstream diagnostic(child_log, std::ios::binary | std::ios::ate);
        const auto end = diagnostic.tellg();
        if (end > 0)
        {
            const auto count = end > 16384 ? std::streamoff{16384} : std::streamoff{end};
            diagnostic.seekg(end - count);
            std::string tail(static_cast<std::size_t>(count), '\0');
            diagnostic.read(tail.data(), count);
            tail.resize(static_cast<std::size_t>(diagnostic.gcount()));
            std::cerr << "Owned child diagnostic (last 16 KiB):\n" << tail << '\n';
        }
        return 1;
    }
}
