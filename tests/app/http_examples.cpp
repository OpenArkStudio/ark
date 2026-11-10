// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <typeinfo>
#include <utility>
#include <vector>

#include <ark/etcd_client/etcd_client_module.hpp>
#include <ark/http/http_server_module.hpp>
#include <ark/module_base.hpp>
#include <ark/net/http_client_module.hpp>
#include <ark/plugin_base.hpp>
#include "../../examples/http_client/http_client_sample_plugin.hpp"
#include "../../examples/http_server/http_server_plugin.hpp"

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::_Exit(1);
    }
}

struct observations {
    unsigned future_posts{};
    unsigned callback_posts{};
    unsigned callbacks{};
    unsigned grants{};
    unsigned clients_destroyed{};
    unsigned updates{};
    unsigned starts{};
    unsigned pre_stops{};
    unsigned stops{};
    std::vector<std::function<void()>> pending;
    std::thread::id owner = std::this_thread::get_id();
};

class client_service final : public ark::module_base<ark::http_client_module> {
public:
    explicit client_service(observations& events)
        : events_(events) {
    }

    std::future<std::pair<bool, std::string>> async_request(arknet::http::verb, const std::string&, uint16_t,
        const std::string&, const std::map<std::string, std::string>*, const std::vector<std::string>*,
        const std::string*, const std::string*, int, int) override {
        require(false, "Example unexpectedly selected async_request");
        return {};
    }

    std::future<std::pair<bool, std::string>> get(const std::string&, uint16_t, const std::string&,
        const std::map<std::string, std::string>*, const std::vector<std::string>*, int, int) override {
        require(false, "Example unexpectedly selected GET");
        return {};
    }

    std::future<std::pair<bool, std::string>> post(const std::string& host, uint16_t port, const std::string& path,
        const std::map<std::string, std::string>* params, const std::string* body, const std::string* content_type,
        const std::vector<std::string>* cookies, int io_timeout, int connect_timeout) override {
        check_request(host, port, path, params, body, content_type, cookies, io_timeout, connect_timeout);
        ++events_.future_posts;
        std::promise<std::pair<bool, std::string>> result;
        auto future = result.get_future();
        result.set_value({true, "future-response"});
        return future;
    }

    void post(std::function<void(std::pair<bool, std::string>)> callback, const std::string& host, uint16_t port,
        const std::string& path, const std::map<std::string, std::string>* params, const std::string* body,
        const std::string* content_type, const std::vector<std::string>* cookies, int io_timeout,
        int connect_timeout) override {
        check_request(host, port, path, params, body, content_type, cookies, io_timeout, connect_timeout);
        ++events_.callback_posts;
        events_.pending.push_back([this, callback = std::move(callback)] {
            require(std::this_thread::get_id() == events_.owner, "HTTP callback left the manager thread");
            callback({true, "callback-response"});
            ++events_.callbacks;
        });
    }

    void post(std::function<void(std::pair<bool, std::string>)>, const std::string&, const std::string&,
        const std::string&, const std::map<std::string, std::string>*, const std::string*, const std::string*,
        const std::vector<std::string>*, int, int) override {
        require(false, "Example unexpectedly selected the service-name overload");
    }

    bool update() override {
        ++events_.updates;
        auto pending = std::move(events_.pending);
        events_.pending.clear();
        for (auto& callback : pending) {
            callback();
        }
        return true;
    }

    bool pre_shut() override {
        ++events_.pre_stops;
        return true;
    }

    bool shut() override {
        ++events_.stops;
        return true;
    }

private:
    static void check_request(const std::string& host, uint16_t port, const std::string& path,
        const std::map<std::string, std::string>* params, const std::string* body, const std::string* content_type,
        const std::vector<std::string>* cookies, int io_timeout, int connect_timeout) {
        require(host == "172.20.3.74" && port == 2379 && path == "/v3/kv/put" && params == nullptr && body &&
                    *body == "{\"key\": \"Zm9v\", \"value\": \"YmFy\"}" && content_type == nullptr &&
                    cookies == nullptr && io_timeout == 20 && connect_timeout == 5,
            "Example POST payload, endpoint or timeout defaults changed");
    }

    observations& events_;
};

class lease_client final : public ark::etcd_client {
public:
    explicit lease_client(observations& events)
        : events_(events) {
    }

    ~lease_client() override {
        ++events_.clients_destroyed;
    }

    void grant(std::function<void(std::pair<int64_t, std::string>)> callback, int ttl) override {
        require(ttl == 100, "Example lease TTL changed");
        ++events_.grants;
        events_.pending.push_back([this, callback = std::move(callback)] {
            require(std::this_thread::get_id() == events_.owner, "Lease callback left the manager thread");
            callback({42, ""});
            ++events_.callbacks;
        });
    }

    void keepalive(std::function<void(std::pair<bool, std::string>)>, int64_t) override {
        require(false, "Example unexpectedly requested keepalive");
    }

private:
    observations& events_;
};

class etcd_service final : public ark::module_base<ark::etcd_client_module> {
public:
    explicit etcd_service(observations& events)
        : events_(events) {
    }

    std::shared_ptr<ark::etcd_client> create_etcd_client(
        std::vector<std::string> endpoints, int io_timeout, int connect_timeout) override {
        require(endpoints == std::vector<std::string>{"172.20.3.74:2379"} && io_timeout == 5 && connect_timeout == 2,
            "Example etcd endpoints or timeout defaults changed");
        return std::make_shared<lease_client>(events_);
    }

private:
    observations& events_;
};

class server_service final : public ark::module_base<ark::http_server_module> {
public:
    explicit server_service(observations& events)
        : events_(events) {
    }

    arknet::http_router& get_router() override {
        return router_;
    }

    bool start_server() override {
        require(false, "Example unexpectedly selected configured start");
        return false;
    }

    bool start_server(const std::string& host, uint16_t port, uint8_t threads) override {
        require(host == "0.0.0.0" && port == 8080 && threads == 4, "Example listener arguments changed");
        ++events_.starts;
        // The historical sample ignores this return value in post_init().
        return false;
    }

private:
    observations& events_;
    arknet::http_router router_;
};

class services final : public ark::plugin_base<services> {
public:
    explicit services(observations& events)
        : events_(events) {
    }

    void install() override {
        require(register_module<ark::http_client_module>(
                    std::make_unique<client_service>(events_), ark::module_update::enabled),
            "HTTP fixture registration failed");
        require(register_module<ark::http_server_module>(
                    std::make_unique<server_service>(events_), ark::module_update::disabled),
            "Server fixture registration failed");
        require(register_module<ark::etcd_client_module>(
                    std::make_unique<etcd_service>(events_), ark::module_update::disabled),
            "Etcd fixture registration failed");
    }

    void uninstall() override {
        require(unregister_module<ark::etcd_client_module>(), "Etcd fixture unregistration failed");
        require(unregister_module<ark::http_server_module>(), "Server fixture unregistration failed");
        require(unregister_module<ark::http_client_module>(), "HTTP fixture unregistration failed");
    }

private:
    observations& events_;
};
} // namespace

int main(int argc, char* argv[]) {
    require(argc == 2, "Expected example plugin directory");
    auto client_factory = ark::make_http_client_sample_module();
    auto server_factory = ark::make_http_server_sample_module();
    auto other_client = ark::make_http_client_sample_module();
    auto other_server = ark::make_http_server_sample_module();
    require(client_factory && server_factory && other_client && other_server &&
                client_factory.get() != other_client.get() && server_factory.get() != other_server.get() &&
                client_factory->get_plugin_manager() == nullptr && server_factory->get_plugin_manager() == nullptr &&
                client_factory->get_name().empty() && server_factory->get_name().empty(),
        "Private factories must return independent, unregistered interface owners");
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    for (int run = 0; run < 2; ++run) {
        observations events;
        ark::plugin_manager manager;
        services fixtures(events);
        fixtures.set_plugin_manager(&manager);
        fixtures.install();
        std::ostringstream output;
        auto* original_output = std::cout.rdbuf(output.rdbuf());
        require(manager.start(0, std::string(argv[1]) + "/", "",
                    {{"http_client_sample_plugin", "client.toml"}, {"http_server_plugin", "server.toml"}}),
            "Actual HTTP example DSO startup failed: " + manager.get_startup_failure());
        auto* client = manager.find_module<ark::http_client_sample_module>();
        auto* server = manager.find_module<ark::http_server_sample_module>();
        require(client && server && client->get_plugin_manager() == &manager &&
                    server->get_plugin_manager() == &manager &&
                    client->get_name() == typeid(ark::http_client_sample_module).name() &&
                    server->get_name() == typeid(ark::http_server_sample_module).name(),
            "Actual example DSO factory registration lost interface identity/ownership");
        require(manager.get_plugin_conf("http_client_sample_plugin") == "client.toml" &&
                    manager.get_plugin_conf("http_server_plugin") == "server.toml",
            "Example plugin configuration identity changed");
        require(events.future_posts == 1 && events.callback_posts == 1 && events.grants == 1 && events.starts == 1 &&
                    events.callbacks == 0 && events.updates == 0,
            "Example request flow changed or callbacks ran before manager update");
        require(output.str().find("Start testing Http Client") < output.str().find("future-response") &&
                    output.str().find("future-response") < output.str().find("Finish testing Http Client") &&
                    output.str().find("callback-response") == std::string::npos,
            "Example must consume its future before startup completes and defer its callback output");
        require(manager.update(1) && manager.update(2) && events.callbacks == 2 && events.updates == 2,
            "Example callbacks must complete exactly once through owner updates");
        require(output.str().find("callback-response") != std::string::npos &&
                    output.str().find("42  |") != std::string::npos,
            "Example callback/lease output changed");
        require(manager.stop(3) && manager.stop(4), "Actual example DSO normal/repeated stop failed");
        require(events.clients_destroyed == 1 && events.pre_stops == 1 && events.stops == 1,
            "Actual example DSO stop retained lease ownership or repeated service shutdown");
        fixtures.uninstall();
        std::cout.rdbuf(original_output);
        std::cout << output.str();
    }
    std::cout << "HTTP examples: actual factories/DSOs, interface identity, requests/default timeouts, "
                 "owner-thread callbacks, listener arguments, lease destruction, repeated stop/reload passed\n";
}
