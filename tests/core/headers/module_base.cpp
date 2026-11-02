// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <ark/module_base.hpp>

static_assert(std::derived_from<ark::module_base<ark::module>, ark::module>);
static_assert(std::same_as<decltype(&ark::module_base<ark::module>::update), decltype(&ark::module::update)>);
static_assert(std::same_as<decltype(&ark::module_base<ark::module>::init), decltype(&ark::module::init)>);
static_assert(std::same_as<decltype(&ark::module_base<ark::module>::post_init), decltype(&ark::module::post_init)>);
static_assert(
    std::same_as<decltype(&ark::module_base<ark::module>::check_config), decltype(&ark::module::check_config)>);
static_assert(std::same_as<decltype(&ark::module_base<ark::module>::pre_update), decltype(&ark::module::pre_update)>);
static_assert(
    std::same_as<decltype(&ark::module_base<ark::module>::ready_to_shut), decltype(&ark::module::ready_to_shut)>);
static_assert(std::same_as<decltype(&ark::module_base<ark::module>::pre_shut), decltype(&ark::module::pre_shut)>);
static_assert(std::same_as<decltype(&ark::module_base<ark::module>::shut), decltype(&ark::module::shut)>);
