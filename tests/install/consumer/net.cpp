// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <type_traits>

#include <ark/net/external_root_callback.hpp>
#include <ark/net/http_client_module.hpp>

#if __has_include(<ark/net/http_client_module_impl.hpp>) || __has_include(<ark/net/http_client_factory.hpp>)
#error "Net implementation headers must not be installed"
#endif

static_assert(std::is_abstract_v<ark::http_client_module>);

int main() {
    return 0;
}
