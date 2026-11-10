// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <type_traits>

#include <ark/net/http_client_module.hpp>
#include <ark/net/net_client_service_impl.hpp>
#include <ark/net/net_server_service_impl.hpp>
#include <ark/net/net_service_manager_module.hpp>
#include <ark/net/tcp_client.hpp>
#include <ark/net/websocket_client.hpp>

static_assert(std::is_base_of_v<ark::module, ark::net_service_manager_module>);
static_assert(std::is_base_of_v<ark::module, ark::http_client_module>);
static_assert(std::is_abstract_v<ark::http_client_module>);
static_assert(std::is_base_of_v<ark::net_client_service, ark::net_client_service_impl>);
static_assert(std::is_base_of_v<ark::net_server_service, ark::net_server_service_impl>);

#if __has_include(<ark/net/http_client_module_impl.hpp>) || __has_include(<ark/net/http_client_factory.hpp>) || \
    __has_include(<ark/net/net_service_manager_module_impl.hpp>) || __has_include(<ark/net/net_plugin.hpp>) || \
    __has_include(<net/net_plugin.hpp>)
#error "Net SDK mapping must exclude HTTP/manager implementations and private plugin assembly"
#endif

struct receiver {
    void on_message(const ark::net_msg*, ark::conv_id_t) {
    }

    void on_websocket_message(const ark::net_msg*, const ark::conv_id_t&) {
    }

    void on_event(const ark::net_event*) {
    }
};

// Instantiate public endpoint callback templates without linking a runtime DSO.
static_assert(requires(receiver* target) {
    ark::tcp_client(target, &receiver::on_message, &receiver::on_event);
    ark::websocket_client(target, &receiver::on_websocket_message, &receiver::on_event);
});

int main() {
    return 0;
}
