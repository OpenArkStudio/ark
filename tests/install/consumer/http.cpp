// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <utility>

#include <dlfcn.h>

#include <ark/http/http_server_module.hpp>
#include <ark/plugin_manager.hpp>
#include <ark/time/date_time.hpp>

static_assert(std::is_base_of_v<ark::module, ark::http_server_module>);
static_assert(std::is_same_v<decltype(std::declval<ark::http_server_module&>().get_router()), arknet::http_router&>);

#if __has_include(<ark/http/http_server_module_impl.hpp>) || __has_include(<ark/http/http_plugin.hpp>) || \
    __has_include(<http/http_plugin.hpp>) || __has_include(<detail/plugin_configuration.hpp>)
#error "HTTP public header mapping must exclude implementation, assembly and parser headers"
#endif

namespace {

[[noreturn]] void fail(const std::string& reason) {
    std::cerr << reason << std::endl;
    // A failed acceptance must not unwind through native owners still in use.
    std::_Exit(1);
}

void require(bool condition, const std::string& reason) {
    if (!condition)
    {
        fail(reason);
    }
}

int make_socket() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    require(fd >= 0, "socket failed");
    timeval timeout{3, 0};
    require(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0, "socket timeout failed");
    return fd;
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 3, "expected installed plugin directory and independent runtime directory");
    const auto plugin_directory = std::filesystem::canonical(argv[1]);
#ifdef _DEBUG
    constexpr auto plugin_filename = "ark_http_plugin_d" ARK_HTTP_TEST_LIBRARY_SUFFIX;
#else
    constexpr auto plugin_filename = "ark_http_plugin" ARK_HTTP_TEST_LIBRARY_SUFFIX;
#endif
    require(std::filesystem::is_regular_file(plugin_directory / plugin_filename),
        "installed HTTP plugin is missing; match CMAKE_BUILD_TYPE to the producer configuration");
    const auto expected_dso = std::filesystem::canonical(plugin_directory / plugin_filename);
    const auto runtime = std::filesystem::absolute(argv[2]) /
                         (std::to_string(::getpid()) + "-" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directories(runtime), "independent runtime directory creation failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int probe = make_socket();
    require(::bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "port probe bind failed");
    socklen_t size = sizeof(address);
    require(::getsockname(probe, reinterpret_cast<sockaddr*>(&address), &size) == 0, "port probe query failed");
    const auto port = ntohs(address.sin_port);
    ::close(probe);
    {
        std::ofstream config(runtime / "http.toml");
        config << "[http]\nhost = '127.0.0.1'\nport = " << port << "\nthreads = 2\n";
        require(config.good(), "HTTP configuration write failed");
    }
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& line) { std::cout << line << std::endl; });
    ark::plugin_manager manager;
    require(manager.start(ark::date_time::get_now_time(), plugin_directory.string() + "/", runtime.string() + "/",
                {{"ark_http_plugin", "http.toml"}}),
        "installed HTTP plugin startup failed: " + manager.get_startup_failure());
    auto* http = manager.try_find_module<ark::http_server_module>();
    require(http, "installed HTTP interface RTTI mismatch");
    Dl_info library{};
    void* const* vtable{};
    // Native UNIX C++ ABIs store the primary vptr first. Copy its representation
    // without accessing the object through an incompatible pointer type.
    std::memcpy(&vtable, static_cast<const void*>(http), sizeof(vtable));
    require(::dladdr(vtable[0], &library) != 0 && library.dli_fname, "HTTP implementation DSO address unavailable");
    require(std::filesystem::canonical(library.dli_fname) == expected_dso, "HTTP implementation came from another DSO");
    std::cout << "HTTP implementation DSO: " << library.dli_fname << '\n';
    auto capture = std::make_shared<int>(42);
    std::weak_ptr<int> retired = capture;
    http->get_router().add(arknet::http::verb::get, "/installed",
        [capture](auto&, auto& response) { response.body() = "installed-http-cxx20"; });
    capture.reset();
    require(http->start_server(), "installed HTTP listener failed");
    std::cout << "HTTP loopback port: " << port << '\n';
    int client = make_socket();
    require(::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "HTTP connect failed");
    const std::string request = "GET /installed HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    std::size_t sent{};
    while (sent != request.size())
    {
        const auto count = ::send(client, request.data() + sent, request.size() - sent, 0);
        require(count > 0, "HTTP send failed");
        sent += static_cast<std::size_t>(count);
    }
    std::string response;
    char bytes[4096];
    ssize_t count;
    while ((count = ::recv(client, bytes, sizeof(bytes), 0)) > 0)
    {
        response.append(bytes, static_cast<std::size_t>(count));
        require(response.size() <= 65536, "HTTP response exceeded the fixture bound");
    }
    require(count == 0, "HTTP receive did not finish normally");
    ::close(client);
    require(
        response.starts_with("HTTP/1.1 200 ") && response.ends_with("installed-http-cxx20"), "HTTP response mismatch");
    std::cout << "HTTP response: 200 installed-http-cxx20\n";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!manager.stop_until(ark::date_time::get_now_time(), deadline))
    {
        require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopping &&
                    manager.get_shutdown_failure().empty() && !manager.get_shutdown_exception() &&
                    std::chrono::steady_clock::now() < deadline,
            "HTTP shutdown failed or reached deadline");
        require(manager.update(ark::date_time::get_now_time()), "HTTP shutdown owner update failed");
        std::this_thread::yield();
    }
    require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped &&
                manager.get_shutdown_failure().empty() && !manager.get_shutdown_exception() && retired.expired(),
        "HTTP shutdown did not destroy the route capture");
    require(manager.stop(ark::date_time::get_now_time()), "HTTP repeated stop failed");
    probe = make_socket();
    require(::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1 && errno == ECONNREFUSED,
        "HTTP endpoint did not refuse connections after stop");
    ::close(probe);
    std::cout << "HTTP state=stopped; route capture destroyed; endpoint closed; repeated stop passed\n";
}
