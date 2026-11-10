// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <ark/plugin_base.hpp>

class compile_plugin final : public ark::plugin_base<compile_plugin> {
public:
    void install() override {
    }

    void uninstall() override {
    }
};

static_assert(std::derived_from<compile_plugin, ark::plugin>);
