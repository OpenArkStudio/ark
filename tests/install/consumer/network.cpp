// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <dlfcn.h>

#include <ark/bus/bus_module.hpp>
#include <ark/bus/msg_module.hpp>
#include <ark/kernel/data_list_impl.hpp>
#include <ark/kernel/kernel_module.hpp>
#include <ark/kernel/map_module.hpp>
#include <ark/net/net_service_manager_module.hpp>
#include <ark/plugin_manager.hpp>
#include <ark/time/date_time.hpp>
#include <ark/utility/guid_module.hpp>

#if __has_include(<ark/net/net_service_manager_module_impl.hpp>) || __has_include(<ark/net/net_plugin.hpp>) || \
    __has_include(<ark/bus/bus_module_impl.hpp>) || __has_include(<ark/utility/guid_module_impl.hpp>) || \
    __has_include(<detail/entity_json_runtime.hpp>)
#error "Installed network consumer must not see private implementations or assembly"
#endif

namespace {

void require(bool condition, const std::string& reason) {
    if (!condition) {
        std::cerr << reason << std::endl;
        // A failed acceptance must retain live callback/DSO owners until exit.
        std::_Exit(1);
    }
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void require_implementation(const ark::module* module, const std::filesystem::path& directory, const char* stem) {
    require(module, "installed network interface RTTI mismatch");
#ifdef _DEBUG
    const auto expected = directory / (std::string(stem) + "_d" ARK_NETWORK_TEST_LIBRARY_SUFFIX);
#else
    const auto expected = directory / (std::string(stem) + ARK_NETWORK_TEST_LIBRARY_SUFFIX);
#endif
    Dl_info library{};
    void* const* vtable{};
    // Native UNIX C++ ABIs store the primary vptr first; copy its representation.
    std::memcpy(&vtable, static_cast<const void*>(module), sizeof(vtable));
    require(::dladdr(vtable[0], &library) != 0 && library.dli_fname, "network implementation DSO address unavailable");
    require(std::filesystem::canonical(library.dli_fname) == std::filesystem::canonical(expected),
        "network implementation came from another DSO");
    std::cout << "Network implementation DSO: " << library.dli_fname << '\n';
}

int make_socket() {
    const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    require(socket >= 0, "socket creation failed");
    return socket;
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 5, "expected installed plugins, independent runtime, canonical Bus and entity resource files");
    const auto plugins = std::filesystem::canonical(argv[1]);
    const auto bus_file = std::filesystem::canonical(argv[3]);
    const auto schema = std::filesystem::canonical(argv[4]);
    const auto runtime = std::filesystem::absolute(argv[2]) /
                         (std::to_string(::getpid()) + "-" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directories(runtime / "resource/meta"), "runtime directory creation failed");
    std::filesystem::copy_file(schema, runtime / "resource/meta/entity_schema.json");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int probe = make_socket();
    require(::bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "port reservation failed");
    socklen_t address_size = sizeof(address);
    require(
        ::getsockname(probe, reinterpret_cast<sockaddr*>(&address), &address_size) == 0, "reserved port unavailable");
    ::close(probe);
    std::ifstream source(bus_file);
    std::string config{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()};
    constexpr auto original_endpoint = "tcp://127.0.0.1:19100";
    const auto endpoint = config.find(original_endpoint);
    require(source.is_open() && !source.bad() && endpoint != std::string::npos,
        "canonical guest-room Bus endpoint unavailable");
    // Keep the canonical schema and selection; replace only its fixture port.
    config.replace(
        endpoint, std::strlen(original_endpoint), "tcp://127.0.0.1:" + std::to_string(ntohs(address.sin_port)));
    {
        std::ofstream output(runtime / "bus.toml");
        output << config;
        require(output.good(), "runtime Bus configuration write failed");
    }
    std::filesystem::current_path(runtime);
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& line) { std::cout << line << std::endl; });
    ark::plugin_manager manager;
    const ark::bus_addr bus_id(1, 1, 31, 1);
    manager.set_bus_id(bus_id.bus_id);
    require(manager.start(ark::date_time::get_now_time(), path_utf8(plugins) + "/", path_utf8(runtime) + "/",
                {{"ark_net_plugin", ""}, {"ark_bus_plugin", "bus.toml"}, {"ark_kernel_plugin", ""},
                    {"ark_utility_plugin", ""}}),
        "installed network startup failed: " + manager.get_startup_failure());
    auto* network = manager.find_module<ark::net_service_manager_module>();
    auto* bus = manager.find_module<ark::bus_module>();
    auto* messages = manager.find_module<ark::msg_module>();
    auto* kernel = manager.find_module<ark::kernel_module>();
    auto* maps = manager.find_module<ark::map_module>();
    auto* guids = manager.find_module<ark::guid_module>();
    require_implementation(network, plugins, "ark_net_plugin");
    require_implementation(bus, plugins, "ark_bus_plugin");
    require_implementation(messages, plugins, "ark_bus_plugin");
    require_implementation(kernel, plugins, "ark_kernel_plugin");
    require_implementation(maps, plugins, "ark_kernel_plugin");
    require_implementation(guids, plugins, "ark_utility_plugin");
    require(bus->get_self_bus_id() == bus_id.bus_id && bus->get_app_type("game") == ark::app_type::ark_app_game &&
                bus->get_self_proc().server_ep.get_port() == ntohs(address.sin_port),
        "canonical Bus configuration did not reach the public module");
    const auto first_guid = guids->create_guid();
    require(first_guid != ark::null_guid && guids->create_guid() != first_guid, "Utility GUID generation failed");
    require(maps->create_map(1), "Kernel map creation failed");
    auto player = kernel->create_entity(ark::null_guid, 1, 0, "GuestPlayer", 1, ark::data_list_impl());
    require(player && player->get_u_int32(104) == 100 && player->set_u_int32(104, 85) && player->get_u_int32(104) == 85,
        "canonical Kernel JSON profile or entity mutation failed");
    const auto snapshot = kernel->capture_scalar_snapshot(*player, std::array<std::uint32_t, 1>{104});
    require(network->create_server(), "installed Net TCP listener failed");
    auto server = network->get_inter_net_server();
    require(server && !network->get_intra_net_server(), "canonical Net listener selection failed");
    const std::weak_ptr<ark::net> endpoint_owner = server->get_net();
    auto capture = std::make_shared<int>(42);
    const std::weak_ptr<int> retired = capture;
    const auto owner = std::this_thread::get_id();
    bool received{};
    const std::string body = "installed-network-cxx20";
    require(server->reg_msg_callback(45000,
                [&, capture](const ark::net_msg* message, ark::conv_id_t session) {
                    require(std::this_thread::get_id() == owner && session != 0 && !received && *capture == 42 &&
                                message->get_msg_id() == 45000 &&
                                std::string(message->get_msg_data(), message->get_msg_length()) == body,
                        "Net callback ownership or frame changed");
                    require(messages->send_to_client(session, 45001, body), "Bus public reply failed");
                    received = true;
                    std::cout << "Network request callback and Bus reply queued on owner thread" << std::endl;
                }),
        "Net public callback registration failed");
    capture.reset();
    const int client = make_socket();
    require(::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "TCP connect failed");
    ark::s_msg_header header;
    header.set_message_id(45000);
    header.set_message_length(static_cast<std::uint32_t>(body.size()));
    std::string frame;
    header.serialize_to_string(frame);
    frame += body;
    std::size_t sent{};
    while (sent != frame.size()) {
        const auto count = ::send(client, frame.data() + sent, frame.size() - sent, 0);
        require(count > 0, "TCP frame send failed");
        sent += static_cast<std::size_t>(count);
    }
    require(!received, "Net callback ran before owner update");
    std::string response;
    const auto request_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (response.size() < frame.size()) {
        require(std::chrono::steady_clock::now() < request_deadline && manager.update(ark::date_time::get_now_time()),
            "TCP request exceeded its owner observation or update failed");
        char bytes[4096];
        const auto count = ::recv(client, bytes, sizeof(bytes), MSG_DONTWAIT);
        require(count > 0 || (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)),
            "TCP reply receive failed");
        if (count > 0) {
            response.append(bytes, static_cast<std::size_t>(count));
            require(response.size() <= frame.size(), "TCP reply exceeded the fixture bound");
        }
        std::this_thread::yield();
    }
    header.parse_from_array(response.data());
    require(received && header.message_id() == 45001 && header.message_length() == body.size() &&
                response.substr(header.header_length()) == body,
        "Net/Bus TCP round trip changed");
    std::cout << "Network TCP round trip received" << std::endl;
    require(kernel->destroy_entity(player->get_id()), "Kernel entity retirement failed");
    player.reset();
    server.reset();
    char unexpected{};
    require(::recv(client, &unexpected, 1, MSG_DONTWAIT) == -1 && (errno == EAGAIN || errno == EWOULDBLOCK),
        "accepted TCP peer closed before shutdown");
    std::cout << "Network entity and consumer service handles released; begin cooperative stop" << std::endl;
    timeval peer_timeout{5, 0};
    require(::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &peer_timeout, sizeof(peer_timeout)) == 0,
        "TCP peer receive timeout setup failed");
    // Native stop sends FIN and waits for the peer's graceful close. Read EOF on
    // an independent peer thread while the manager owner performs synchronous
    // cleanup; closing this peer first would hide a retained accepted socket.
    std::thread peer([client] {
        char byte{};
        ssize_t count{};
        do {
            count = ::recv(client, &byte, 1, 0);
        } while (count < 0 && errno == EINTR);
        require(count == 0, "Net shutdown did not send EOF to the accepted TCP peer");
        ::close(client);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!manager.stop_until(ark::date_time::get_now_time(), deadline)) {
        require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopping &&
                    manager.get_shutdown_failure().empty() && !manager.get_shutdown_exception() &&
                    std::chrono::steady_clock::now() < deadline,
            "network shutdown failed or reached deadline");
        require(manager.update(ark::date_time::get_now_time()), "network shutdown owner update failed");
        std::this_thread::yield();
    }
    require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped &&
                manager.get_shutdown_failure().empty() && !manager.get_shutdown_exception() &&
                !manager.try_find_module<ark::net_service_manager_module>() &&
                !manager.try_find_module<ark::bus_module>() && !manager.try_find_module<ark::kernel_module>() &&
                !manager.try_find_module<ark::guid_module>() && endpoint_owner.expired() && retired.expired() &&
                snapshot.class_name == "GuestPlayer" && std::get<std::uint32_t>(snapshot.fields.at(104)) == 85,
        "network unload, owner retirement or owning snapshot failed");
    peer.join();
    probe = make_socket();
    require(::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1 && errno == ECONNREFUSED,
        "Net listener did not refuse connections after stop");
    ::close(probe);
    require(manager.stop(ark::date_time::get_now_time()), "network repeated stop failed");
    std::cout << "Network: canonical Bus/Kernel resources; Utility GUID; one owner-thread TCP/Bus round trip; "
                 "state=stopped; modules removed; callback/endpoint owners retired; snapshot survived; "
                 "accepted connection and listener closed; repeated stop passed"
              << std::endl;
}
