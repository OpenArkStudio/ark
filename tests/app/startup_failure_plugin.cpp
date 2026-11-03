// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <arknet/tcp/tcp_server.hpp>

#include <ark/plugin_manager.hpp>

namespace {

class startup_failure_module final : public ark::module
{
public:
    void start_owned_io() {
        // Registration owns this real listener until DllExitPlugin, just as
        // runtime plugins own modules that can survive a partial startup.
        endpoint_ = std::make_unique<arknet::tcp_server>(1024, 4096, 1);
        if (!endpoint_->start("127.0.0.1", 0) || !endpoint_->is_started() || endpoint_->get_listen_port() == 0)
        {
            throw std::runtime_error("Startup test endpoint did not start");
        }
        std::cerr << "app-startup-test:owned_io_started port=" << endpoint_->get_listen_port() << '\n';
    }

    void fail_at(std::string phase) {
        const auto separator = phase.find(':');
        behavior_ = separator == std::string::npos ? "false" : phase.substr(0, separator);
        failed_phase_ = separator == std::string::npos ? std::move(phase) : phase.substr(separator + 1);
    }

    const std::string& get_name() const override {
        static const std::string name = "app_startup_failure_module";
        return name;
    }

    bool init() override {
        return record("init");
    }

    bool post_init() override {
        return record("post_init");
    }

    bool check_config() override {
        return record("check_config");
    }

    bool pre_update() override {
        return record("pre_update");
    }

    bool update() override {
        // Record forbidden entry only once, keeping failure output bounded.
        if (!updated_)
        {
            updated_ = true;
            record("update");
        }
        return true;
    }

    bool pre_shut() override {
        return record("pre_shut");
    }

    bool shut() override {
        return record("shut");
    }

private:
    bool record(std::string_view phase) {
        std::cerr << "app-startup-test:" << phase << '\n';
        if (phase == failed_phase_)
        {
            if (behavior_ == "throw")
            {
                throw std::runtime_error("test startup exception in " + std::string(phase));
            }
            if (behavior_ == "unknown")
            {
                throw 7;
            }
            return false;
        }
        return true;
    }

    std::unique_ptr<arknet::tcp_server> endpoint_;
    std::string failed_phase_;
    std::string behavior_;
    bool updated_{};
};

// Match runtime module ownership: only plugin uninstall deletes the instance.
startup_failure_module* fixture{};

} // namespace

// The real application loads this test-only module through its normal DLL API.
ARK_EXPORT_FUNC void DllEntryPlugin(
    ark::plugin_manager* manager, const std::string& plugin_name, ark::logger* shared_logger) {
    ark::logger::init(shared_logger);
    const auto& mode = manager->get_plugin_conf(plugin_name);
    if (mode == "utf8:init")
    {
        if (manager->get_app_name() != "应用-é-🧪")
        {
            throw std::runtime_error("Launcher lost UTF-8 application-name arguments");
        }
#ifdef ARK_PLATFORM_WIN
        // A shared console title can change in another process. Check it only
        // when this child owns its console, avoiding cross-test interference.
        DWORD console_process{};
        if (GetConsoleProcessList(&console_process, 1) == 1 && console_process == GetCurrentProcessId())
        {
            wchar_t title[256]{};
            const auto expected_title = L"应用-é-🧪-1.0.1.1-" + std::to_wstring(manager->get_bus_id());
            if (GetConsoleTitleW(title, 256) == 0 || std::wstring_view(title) != expected_title)
            {
                throw std::runtime_error("Launcher lost the native Unicode console title");
            }
        }
#endif
        using ark::logger;
        ARK_LOG_INFO("{}", "launcher UTF-8 日志标记-é-🧪");
        std::cerr << "app-startup-test:unicode_arguments_verified\n";
    }
    fixture = new startup_failure_module;
    fixture->fail_at(mode);
    fixture->start_owned_io();
    if (!manager->add_module(fixture->get_name(), fixture) || !manager->add_update_module(fixture))
    {
        std::cerr << "Startup test module registration failed\n";
        std::abort();
    }
    if (mode == "throw:load_plugin")
    {
        std::cerr << "app-startup-test:load_plugin\n";
        throw std::runtime_error("test plugin entry exception");
    }
}

ARK_EXPORT_FUNC void DllExitPlugin(ark::plugin_manager* manager) {
    std::cerr << "app-startup-test:uninstall\n";
    manager->remove_update_module(fixture->get_name());
    manager->remove_module(fixture->get_name());
    delete std::exchange(fixture, nullptr);
}
