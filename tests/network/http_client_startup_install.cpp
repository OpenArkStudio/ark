// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include "ark/plugin_base.hpp"
#include "http_client_startup_access.hpp"

namespace {
ark::http_client_module* installed_client{};

struct detach_guard
{
    ~detach_guard() {
        std::fputs("forbidden HTTP fixture destruction\n", stderr);
    }
} retained_dso;

class install_failure_plugin final : public ark::plugin_base<install_failure_plugin>
{
public:
    void install() override {
        client_ = ark::detail::make_http_client_module(get_plugin_manager());
        installed_client = client_.get();
        throw std::runtime_error("controlled HTTP install failure");
    }

    void uninstall() override {
        std::fputs("forbidden HTTP fixture uninstall\n", stderr);
    }

private:
    std::unique_ptr<ark::http_client_module> client_;
};
} // namespace

ARK_EXPORT_FUNC void DllEntryPlugin(ark::plugin_manager* manager, const std::string& name, ark::logger* logger) {
    ark::logger::init(logger);
    manager->register_plugin<install_failure_plugin>(name);
}

ARK_EXPORT_FUNC bool http_fixture_runtime_started() {
    return installed_client && ark::http_client_startup_test_access::runtime(*installed_client).started();
}

ARK_EXPORT_FUNC bool http_fixture_runtime_stopped() {
    return installed_client && ark::http_client_startup_test_access::runtime(*installed_client).stopped();
}
