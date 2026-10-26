// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <type_traits>
#include <utility>

#include <ark/container/array_map.hpp>

// Keep the second inclusion separate to retain the include-guard check.
#include <ark/container/array_map.hpp>

template class ark::array_map<std::string, int>;

#if defined(ARK_CPP11_OR_GREATER) || defined(ARK_CPP14_OR_GREATER) || defined(ARK_CPP17_OR_GREATER) ||                 \
    defined(ARK_NOEXCEPT) || defined(ARK_CONSTEXPR) || defined(ARK_FINAL) || defined(MAKE_BUILDIN_TYPE)
#error "Use C++20 keywords and explicit type traits instead of removed compatibility macros"
#endif

// Instantiate iterator bodies too: the declared get_data return type alone
// cannot catch an incompatible mapped value returned from its implementation.
template class ark::pod_string_iterator<char, std::size_t>;
template class ark::pod_string_iterator<char, int*>;

using scalar_iterator = ark::pod_string_iterator<char, std::size_t>;
using pointer_iterator = ark::pod_string_iterator<char, int*>;
static_assert(std::is_same_v<decltype(std::declval<scalar_iterator&>()++), scalar_iterator>);
static_assert(std::is_same_v<decltype(++std::declval<scalar_iterator&>()), scalar_iterator&>);
static_assert(std::is_same_v<decltype(std::declval<const scalar_iterator&>().get_data()), std::size_t*>);
static_assert(std::is_same_v<decltype(std::declval<pointer_iterator&>()++), pointer_iterator>);
static_assert(std::is_same_v<decltype(std::declval<const pointer_iterator&>().get_data()), int**>);
