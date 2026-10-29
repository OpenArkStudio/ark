// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ark/plugin_manager.hpp>
#include <ark/time/timer.hpp>

namespace {

void mark(const char* value) {
    std::fprintf(stderr, "startup-cleanup:%s\n", value);
    std::fflush(stderr);
}

struct library_guard
{
    ~library_guard() {
        mark("forbidden_dso_destructor");
    }
} guard;

struct wheel_context
{
    std::unique_ptr<ark::time_wheel_manager> wheel;
    std::string mode;

    ~wheel_context() {
        mark("forbidden_capsule_destructor");
    }
};

ark::startup_cleanup_result release(void* pointer) {
    auto& context = *static_cast<wheel_context*>(pointer);
    mark("release_enter");
    if (context.mode == "unsafe:init")
    {
        return ark::startup_cleanup_result::unsafe;
    }
    if (context.mode == "cleanup_throw:init")
    {
        throw 4;
    }
    if (context.wheel)
    {
        context.wheel->shut();
        context.wheel.reset();
    }
    mark("release_done");
    return ark::startup_cleanup_result::done;
}

class isolated_wheel_module final : public ark::module
{
public:
    explicit isolated_wheel_module(std::shared_ptr<wheel_context> context)
        : context_(std::move(context)) {
        if (context_->mode == "throw:before_acquire")
        {
            throw std::runtime_error("constructor before acquire");
        }
        context_->wheel = std::make_unique<ark::time_wheel_manager>();
        if (context_->mode == "throw:partial_acquire")
        {
            throw std::runtime_error("constructor partial acquire");
        }
        context_->wheel->init(1);
        mark("acquired");
        if (context_->mode == "throw:after_acquire")
        {
            throw std::runtime_error("constructor after acquire");
        }
    }

    ~isolated_wheel_module() override {
        mark("forbidden_module_destructor");
    }

    const std::string& get_name() const override {
        static const std::string name = "isolated_wheel";
        return name;
    }

    bool init() override {
        return step("init");
    }

    bool post_init() override {
        return step("post_init");
    }

    bool check_config() override {
        return step("check_config");
    }

    bool pre_update() override {
        return step("pre_update");
    }

    bool update() override {
        mark("forbidden_update");
        return true;
    }

    bool pre_shut() override {
        mark("forbidden_pre_shut");
        return true;
    }

    bool shut() override {
        mark("forbidden_shut");
        return true;
    }

private:
    bool step(std::string_view phase) {
        mark(std::string(phase).c_str());
        const auto separator = context_->mode.find(':');
        if (std::string_view(context_->mode).substr(separator + 1) == phase)
        {
            if (context_->mode.starts_with("throw:"))
            {
                throw std::runtime_error("wheel hook failure");
            }
            return false;
        }
        return true;
    }

    std::shared_ptr<wheel_context> context_;
};

} // namespace

ARK_EXPORT_FUNC void DllEntryPlugin(ark::plugin_manager* manager, const std::string& name, ark::logger* shared_logger) {
    ark::logger::init(shared_logger);
    auto context = std::make_shared<wheel_context>();
    context->mode = manager->get_plugin_conf(name);
    if (!manager->prepare_startup_cleanup("isolated_wheel", name, context, release))
    {
        throw std::runtime_error("cleanup enrollment rejected before acquisition");
    }
    // The host already holds a complete independent capsule if this constructor
    // throws. Only normal successful registration owns the complete module.
    auto module = std::make_unique<isolated_wheel_module>(context);
    if (!manager->add_module(module->get_name(), module.get()))
    {
        throw std::runtime_error("module registration failed");
    }
    module.release();
    if (context->mode == "throw:install")
    {
        throw std::runtime_error("partially published installation");
    }
}

ARK_EXPORT_FUNC void DllExitPlugin(ark::plugin_manager*) {
    mark("forbidden_uninstall");
}
