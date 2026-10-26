// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <type_traits>

#include <ark/etcd_client/etcd_client.hpp>
#include <ark/etcd_client/etcd_client_impl.hpp>
#include <ark/etcd_client/etcd_client_module.hpp>
#include <ark/etcd_client/etcd_client_module_impl.hpp>
#include <ark/etcd_client/etcd_client_plugin.hpp>

static_assert(std::is_base_of_v<ark::etcd_client, ark::etcd_client_impl>);
static_assert(std::is_base_of_v<ark::module_base<ark::etcd_client_module>, ark::etcd_client_module_impl>);
static_assert(std::is_base_of_v<ark::plugin_base<ark::etcd_client_plugin>, ark::etcd_client_plugin>);

#if __has_include(<ark/etcd_client/etcd_client_factory.hpp>)
#error "Etcd Client private factory must not be mapped or installed"
#endif

int main() {
    return 0;
}
