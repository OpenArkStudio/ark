// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <ark/lexical_cast.hpp>
#include <ark/module.hpp>
#include <ark/net/tcp_client.hpp>
#include <ark/net/tcp_server.hpp>
#include <ark/plugin_manager.hpp>
#include <ark/string.hpp>
#include <ark/uid_generator.hpp>

static_assert(std::is_base_of_v<ark::net, ark::tcp_client>);
static_assert(std::is_base_of_v<ark::net, ark::tcp_server>);
#ifdef ARK_USE_GUID_32
static_assert(sizeof(ark::guid_t) == 4);
#else
static_assert(sizeof(ark::guid_t) == 8);
#endif

namespace {

class sample_module final : public ark::module {
public:
    bool init() override {
        initialized_ = true;
        return true;
    }

    bool initialized() const {
        return initialized_;
    }

private:
    bool initialized_ = false;
};

} // namespace

bool check_packaged_dependencies();

int main() {
    if (!check_packaged_dependencies()) {
        return 9;
    }
    sample_module service;
    if (!service.init() || !service.initialized() || !service.update() || !service.shut()) {
        return 1;
    }
    if (ark::plugin_manager::instance()->get_app_name() != "ark") {
        return 2;
    }
    // This also detects accidental recursion between conversion helpers and types.
    if (!::lexical_cast<bool>(std::string("true")) || ::lexical_cast<bool>("false") ||
        ::lexical_cast<int>(std::string("42")) != 42) {
        return 3;
    }
    try {
        (void)::lexical_cast<bool>("nope");
        return 4;
    } catch (const std::invalid_argument&) {
    }
    ark::small_string<char, 8> text("abc");
    text.append("defghijkl");
    if (std::string(text.c_str()) != "abcdefghijkl" || text.find("def") != 3) {
        return 5;
    }
    auto copy = text;
    text.clear();
    if (!text.empty() || std::string(copy.c_str()) != "abcdefghijkl") {
        return 6;
    }
    ark::data_node_name node_name("a name longer than the inline buffer");
    if (std::string(node_name.c_str()) != "a name longer than the inline buffer") {
        return 7;
    }
#ifdef ARK_USE_GUID_32
    ark::uid_generator_thread_safe ids;
    const auto id = ids.get_uid();
    if (ids.parse_uid(id) != "ID=" + std::to_string(id)) {
        return 8;
    }
#endif
    std::cout << "SDK consumer checks passed\n";
    return 0;
}
