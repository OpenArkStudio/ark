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
    try
    {
        require(argc == 5, "Expected launcher, plugin directory, source root and fresh runtime directory");
        const auto program = std::filesystem::absolute(argv[1]).string();
        const auto plugins = std::filesystem::absolute(argv[2]).string();
        const auto source = std::filesystem::absolute(argv[3]);
        const auto runtime = std::filesystem::absolute(argv[4]);
        require(std::filesystem::create_directory(runtime), "Runtime directory must be fresh");
        std::filesystem::permissions(runtime, std::filesystem::perms::owner_all);
        const auto log_path = runtime / "output.log";
        unsigned short port = 0;
        {
            socket_owner reservation;
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
        // The launcher owns the bind. A collision after releasing the reservation
        // is a test failure, never permission to stop an unrelated listener.
        std::ofstream manifest(runtime / "application.toml");
        manifest << "[application]\nplugin_library_dir = '" << plugins << "/'\nplugin_config_dir = './'\n"
                 << "[[plugins]]\nname = 'ark_utility_plugin'\nconfig = ''\n"
                 << "[[plugins]]\nname = 'ark_bus_plugin'\nconfig = 'bus.toml'\n"
                 << "[[plugins]]\nname = 'ark_net_plugin'\nconfig = ''\n"
                 << "[[plugins]]\nname = 'ark_login_plugin'\nconfig = ''\n";
        manifest.close();
        std::ofstream bus(runtime / "bus.toml");
        bus << "[bus]\nrelations = []\n[[bus.applications]]\nname = 'login'\ntype = 32\n"
            << "[[bus.processes]]\nbus_id = '1.1.32.1'\nserver_endpoint = 'tcp://127.0.0.1:" << port
            << "'\nintranet_endpoint = ''\nmax_connections = 8\nthreads = 1\n"
            << "[bus.registry]\nhost = '127.0.0.1'\nport = 0\nservice_name = 'unused'\n"
            << "check_interval = '10s'\ncheck_timeout = '3s'\n";
        bus.close();
        const auto app_conf = "--app_conf=" + (runtime / "application.toml").string();
        const auto log_conf = "--log_conf=" + (source / "config/log.conf").string();
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
            ::execl(program.c_str(), program.c_str(), "--busid=1.1.32.1", "--name=login", app_conf.c_str(),
                log_conf.c_str(), static_cast<char*>(nullptr));
            std::_Exit(126);
        }
        const auto startup_deadline = std::chrono::steady_clock::now() + 10s;
        for (;;)
        {
            const auto waited = ::waitpid(child.pid, nullptr, WNOHANG);
            if (waited == child.pid)
            {
                child.pid = -1;
                throw std::runtime_error("Launcher exited before readiness");
            }
            require(waited == 0, "Launcher startup wait failed");
            if (read_log(log_path).find("Start net server successful") != std::string::npos)
            {
                break;
            }
            require(std::chrono::steady_clock::now() < startup_deadline, "Launcher startup timed out");
            std::this_thread::sleep_for(10ms);
        }
        socket_owner peer;
        require(connect_to(peer.value, port) == 0, "Real Login loopback listener did not accept a connection");
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
        std::cout << "Actual Utility/Bus/Net/Login DSOs: loopback connection, signal shutdown, peer closure, "
                     "listener refusal and exit 0 passed; no login business handler claimed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
