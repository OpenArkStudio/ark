// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>

#include "../../src/application/startup_failure.hpp"
#include "ark/net/rudp_server.hpp"
#include "ark/net/tcp_server.hpp"
#include "ark/plugin_base.hpp"

// Observe the real private map without exposing a production injection API.
#include "../../src/plugins/net/net_service_manager_module_impl.cpp"

namespace ark {
struct net_listener_startup_test_access
{
    static auto* candidate(net_service_manager_module& module) {
        auto& impl = static_cast<net_service_manager_module_impl&>(module);
        return impl.server_services_.empty() ? nullptr : &impl.server_services_.begin()->second;
    }

    static bool native_started(net_service_manager_module& module) {
        const auto* entry = candidate(module);
        if (!entry)
        {
            return false;
        }
        auto network = entry->service->get_net();
        if (auto* tcp = dynamic_cast<tcp_server*>(network.get()))
        {
            return tcp->server_ && tcp->server_->iopool().started() &&
                   tcp->server_->iopool().get(0)->get_thread_id() != std::thread::id{};
        }
        if (auto* rudp = dynamic_cast<rudp_server*>(network.get()))
        {
            return rudp->server_ && rudp->server_->iopool().started() &&
                   rudp->server_->iopool().get(0)->get_thread_id() != std::thread::id{};
        }
        return false;
    }

    static bool worker_initialized(net_service_manager_module& module) {
        const auto* entry = candidate(module);
        if (!entry)
        {
            return false;
        }
        // operator new may be reached while io_pool::start holds its lifecycle
        // mutex. Observe the fixture's single lane without reentering started().
        auto network = entry->service->get_net();
        if (auto* tcp = dynamic_cast<tcp_server*>(network.get()))
        {
            return tcp->server_ && tcp->server_->iopool().get(0)->get_thread_id() != std::thread::id{};
        }
        if (auto* rudp = dynamic_cast<rudp_server*>(network.get()))
        {
            return rudp->server_ && rudp->server_->iopool().get(0)->get_thread_id() != std::thread::id{};
        }
        return false;
    }

    static std::uint16_t port(net_server_service& service) {
        auto network = service.get_net();
        if (auto* tcp = dynamic_cast<tcp_server*>(network.get()))
        {
            return tcp->server_->listen_port();
        }
        if (auto* rudp = dynamic_cast<rudp_server*>(network.get()))
        {
            return rudp->server_->listen_port();
        }
        return 0;
    }
};
} // namespace ark

namespace {
using access = ark::net_listener_startup_test_access;
thread_local bool fail_allocations{};
thread_local bool fail_after_worker{};
ark::net_service_manager_module* observed_module{};
std::weak_ptr<ark::net_server_service_impl> observed_service;
bool failure_child{};

void before_allocation() {
    if (fail_after_worker && observed_module && access::worker_initialized(*observed_module))
    {
        // Keep only a weak observation: the test must not rescue a missing owner.
        observed_service = access::candidate(*observed_module)->service;
        fail_after_worker = false;
        fail_allocations = true;
    }
    if (fail_allocations)
    {
        throw std::bad_alloc();
    }
}

void require(bool condition, const char* message) {
    if (!condition)
    {
        ark::app_detail::write_native_stderr(message);
        ark::app_detail::write_native_stderr("\n");
        std::_Exit(2);
    }
}

void forbidden_cleanup() {
    if (failure_child)
    {
        ark::app_detail::write_native_stderr("forbidden Net listener ordinary cleanup\n");
    }
}

struct fixture_bus final : ark::module_base<ark::bus_module>
{
    ark::proc_config proc;
    ark::reg_center registry;
    std::string name{"fixture"};

    const std::string get_app_whole_name(ark::bus_id_t) override {
        return name;
    }

    const std::string& get_app_name(const ark::app_type&) override {
        return name;
    }

    ark::app_type get_app_type(const std::string&) override {
        return ark::app_type::ark_app_game;
    }

    const ark::proc_config& get_self_proc() override {
        return proc;
    }

    ark::app_type get_self_app_type() override {
        return ark::app_type::ark_app_game;
    }

    ark::bus_id_t get_self_bus_id() override {
        return proc.bus_id;
    }

    const std::string get_self_bus_name() override {
        return name;
    }

    int combine_bus_id(ark::app_type, std::uint8_t) override {
        return 1;
    }

    const ark::reg_center& get_reg_center() override {
        return registry;
    }

    bool get_target_bus_relations(std::vector<ark::app_type>&) override {
        return true;
    }

    bool get_other_proc(ark::bus_id_t, ark::proc_config&) const override {
        return false;
    }

    int get_other_proc(ark::app_type, std::vector<const ark::proc_config*>&) const override {
        return 0;
    }
};

struct fixture_timer final : ark::module_base<ark::timer_module>
{
    bool remove_timer(std::uint64_t) override {
        forbidden_cleanup();
        return true;
    }

    std::uint64_t add_single_timer(
        const ark::guid_t&, std::chrono::milliseconds, std::uint32_t, ark::timer_functor&&) override {
        return 1;
    }

    std::uint64_t add_forever_timer(const ark::guid_t&, std::chrono::milliseconds, ark::timer_functor&&) override {
        return 1;
    }
};

struct fixture_msg final : ark::module_base<ark::msg_module>
{
    bool send_def_cs_msg_by_app_type(ark::app_type, ark::msg_id_t, const google::protobuf::Message&) override {
        return false;
    }

    bool send_def_ss_msg_by_app_type(ark::app_type, ark::msg_id_t, const google::protobuf::Message&) override {
        return false;
    }

    bool send_msg_by_app_type(ark::app_type, ark::msg_header*, const google::protobuf::Message&) override {
        return false;
    }

    bool send_msg_by_bus_id(
        ark::bus_id_t, ark::msg_id_t, const google::protobuf::Message&, const ark::guid_t&) override {
        return false;
    }

    bool send_to_client(ark::conv_id_t, ark::msg_id_t, const google::protobuf::Message&, const ark::guid_t&) override {
        return false;
    }

    bool send_to_client(ark::conv_id_t, ark::msg_id_t, const std::string&, bool, const ark::guid_t&) override {
        return false;
    }

    bool send_to_intra(ark::conv_id_t, ark::msg_id_t, const google::protobuf::Message&, const ark::guid_t&) override {
        return false;
    }
};

struct consumer_interface : ark::module
{
};

struct consumer final : ark::module_base<consumer_interface, ark::net_service_manager_module>
{
    ark::net_service_manager_module* network{};

    bool init() override {
        if (failure_child)
        {
            observed_module = network;
            fail_after_worker = true;
            return network->create_server(60000, ark::s_msg_header{}, ark::ss_msg_header{});
        }
        return true;
    }

    ~consumer() override {
        forbidden_cleanup();
    }

    bool update() override {
        forbidden_cleanup();
        return true;
    }

    bool pre_shut() override {
        forbidden_cleanup();
        return true;
    }

    bool shut() override {
        forbidden_cleanup();
        return true;
    }
};

struct fixture_plugin final : ark::plugin_base<fixture_plugin>
{
    fixture_bus* bus{};
    ark::net_service_manager_module* network{};

    void install() override {
        auto provider = std::make_unique<fixture_bus>();
        bus = provider.get();
        require(register_module<ark::bus_module>(std::move(provider), ark::module_update::disabled), "install Bus");
        require(register_module<ark::timer_module>(std::make_unique<fixture_timer>(), ark::module_update::disabled),
            "install Timer");
        require(register_module<ark::msg_module>(std::make_unique<fixture_msg>(), ark::module_update::disabled),
            "install Msg");
        auto module = ark::detail::make_net_service_manager_module();
        network = module.get();
        require(register_module(std::move(module), ark::module_update::enabled), "install Net");
        auto receiver = std::make_unique<consumer>();
        receiver->network = network;
        require(register_module(std::move(receiver), ark::module_update::enabled), "install consumer");
    }

    void uninstall() override {
        forbidden_cleanup();
    }

    ~fixture_plugin() override {
        forbidden_cleanup();
    }
};

void configure(fixture_bus& bus, ark::proto_type protocol, bool intra = false) {
    bus.proc.bus_id = 1;
    bus.proc.thread_num = 1;
    bus.proc.max_connection = 4;
    bus.proc.server_ep.set_port(intra ? 1 : 0);
    bus.proc.intranet_ep.set_port(intra ? 0 : 1);
    auto& endpoint = intra ? bus.proc.intranet_ep : bus.proc.server_ep;
    endpoint.set_ip("127.0.0.1");
    endpoint.set_protocol(protocol);
}

bool create(ark::net_service_manager_module& module) {
    return module.create_server(60000, ark::s_msg_header{}, ark::ss_msg_header{});
}

void successful(ark::proto_type protocol, bool intra) {
    ark::plugin_manager manager;
    fixture_plugin plugin;
    plugin.set_plugin_manager(&manager);
    plugin.install();
    configure(*plugin.bus, protocol, intra);
    require(manager.start(0, {}, {}, {}), "normal manager start");
    require(create(*plugin.network), "listener start");
    auto service = intra ? plugin.network->get_intra_net_server() : plugin.network->get_inter_net_server();
    require(service && service->get_net()->is_working() && access::port(*service), "published listener getter");
    require(intra ? !plugin.network->get_inter_net_server() : !plugin.network->get_intra_net_server(),
        "empty opposite getter");
    const auto original_port = access::port(*service);
    require(!create(*plugin.network), "duplicate must return false before another native start");
    auto unchanged = intra ? plugin.network->get_intra_net_server() : plugin.network->get_inter_net_server();
    require(unchanged == service && access::port(*service) == original_port, "duplicate preserved listener");
    require(manager.update(0), "published listener update");
    service.reset();
    unchanged.reset();
    require(manager.stop(0), "ordinary listener shutdown");
}

void rejected(ark::proto_type protocol) {
    ark::plugin_manager manager;
    fixture_plugin plugin;
    plugin.set_plugin_manager(&manager);
    plugin.install();
    configure(*plugin.bus, protocol);
    require(manager.start(0, {}, {}, {}), "false manager start");
    // A local occupied endpoint produces a real native false return. Windows
    // must reserve exclusively because its SO_REUSEADDR differs from POSIX.
    asio::io_context context;
    asio::ip::tcp::acceptor tcp(context);
    asio::ip::udp::socket udp(context);
    const bool stream = protocol == ark::proto_type::tcp;
    if (stream)
    {
        tcp.open(asio::ip::tcp::v4());
    }
    else
    {
        udp.open(asio::ip::udp::v4());
    }
#if defined(_WIN32)
    const int exclusive = 1;
    const auto handle = stream ? tcp.native_handle() : udp.native_handle();
    require(::setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                sizeof(exclusive)) == 0,
        "exclusive fixture endpoint");
#endif
    if (stream)
    {
        tcp.bind({asio::ip::address_v4::loopback(), 0});
        tcp.listen();
    }
    else
    {
        udp.bind({asio::ip::address_v4::loopback(), 0});
    }
    plugin.bus->proc.server_ep.set_port(stream ? tcp.local_endpoint().port() : udp.local_endpoint().port());
    require(!create(*plugin.network), "occupied listener must return false");
    require(!plugin.network->get_inter_net_server() && !access::candidate(*plugin.network),
        "false candidate removed after ordinary cleanup");
    if (stream)
    {
        tcp.close();
    }
    else
    {
        udp.close();
    }
    require(create(*plugin.network), "same slot retries after ordinary false");
    require(plugin.network->get_inter_net_server() != nullptr, "retry published getter");
    require(manager.stop(0), "false/retry ordinary stop");
}

[[noreturn]] void allocation_failure(ark::proto_type protocol, bool retry_probe) {
    ark::plugin_manager manager;
    fixture_plugin plugin;
    plugin.set_plugin_manager(&manager);
    plugin.install();
    configure(*plugin.bus, protocol);
    failure_child = true;
    bool caught{};
    // Prepare owning call arguments before injection; manager.start is never retried.
    const std::string directory;
    const std::unordered_map<std::string, std::string> plugins;
    try
    { manager.start(0, directory, directory, plugins); }
    catch (const std::bad_alloc&)
    { caught = true; }
    require(caught && fail_allocations && !fail_after_worker,
        "start did not propagate persistent allocation failure after native workers");
    const auto* entry = access::candidate(*plugin.network);
    require(entry && !entry->published && !observed_service.expired(), "missing complete unpublished listener owner");
    require(access::native_started(*plugin.network), "native worker owner lost before runner");
    require(!plugin.network->get_inter_net_server() && !plugin.network->get_intra_net_server(),
        "unpublished candidate escaped getter");
    if (retry_probe)
    {
        // Separate compatibility child: allow the API's new owning header copies
        // to test slot rejection, then restore failure for the emergency exit.
        fail_allocations = false;
        require(!create(*plugin.network), "unsafe candidate slot admitted a second start");
        fail_allocations = true;
    }
    require(
        manager.get_startup_call().result == ark::startup_call_result::exception, "missing startup exception record");
    ark::app_detail::write_native_stderr(
        "Net listener ownership: complete unpublished owner and worker retained; runner exit 1\n");
    ark::app_detail::exit_startup_failure(manager, "controlled listener allocation failure");
}
} // namespace

void* operator new(std::size_t size) {
    before_allocation();
    if (void* pointer = std::malloc(size ? size : 1))
    {
        return pointer;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ::operator new(size, std::nothrow);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

int main(int argc, char** argv) {
    ark::logger logger;
    ark::logger::init(&logger);
    logger.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    std::set_terminate([] {
        ark::app_detail::write_native_stderr("unexpected Net listener terminate\n");
#if defined(_WIN32)
        ::TerminateProcess(::GetCurrentProcess(), 86);
#endif
        std::_Exit(86);
    });
    if (argc == 2 && std::strcmp(argv[1], "normal") == 0)
    {
        for (const auto protocol : {ark::proto_type::tcp, ark::proto_type::udp})
        {
            successful(protocol, false);
            successful(protocol, true);
            rejected(protocol);
        }
        std::fputs("Net listener normal controls passed\n", stderr);
        return 0;
    }
    if (argc == 3 && (std::strcmp(argv[1], "exception") == 0 || std::strcmp(argv[1], "retry") == 0))
    {
        if (std::strcmp(argv[2], "tcp") == 0)
        {
            allocation_failure(ark::proto_type::tcp, std::strcmp(argv[1], "retry") == 0);
        }
        if (std::strcmp(argv[2], "rudp") == 0)
        {
            allocation_failure(ark::proto_type::udp, std::strcmp(argv[1], "retry") == 0);
        }
    }
    return 2;
}
