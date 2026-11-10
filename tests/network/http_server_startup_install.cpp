// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdio>
#include <memory>
#include <stdexcept>

#include "../../src/plugins/http/http_server_module_impl.cpp"

namespace ark {

struct http_server_startup_test_access {
    static auto state(http_server_module& module) {
        return static_cast<http_server_module_impl&>(module).state_;
    }
};

} // namespace ark

namespace {

std::weak_ptr<void> retained_state;
std::weak_ptr<void> retained_capture;
arknet::http_server* retained_native{};

struct destruction_marker {
    ~destruction_marker() {
        std::fputs("forbidden HTTP ownership fixture capture destruction\n", stderr);
    }
};

struct detach_marker {
    ~detach_marker() {
        std::fputs("forbidden HTTP ownership fixture DSO destruction\n", stderr);
    }
} retained_dso;

class http_ownership_install_failure final : public ark::plugin_base<http_ownership_install_failure> {
public:
    void install() override {
        auto candidate = ark::detail::make_http_server_module(get_plugin_manager());
        auto capture = std::make_shared<destruction_marker>();
        retained_capture = capture;
        candidate->get_router().add(arknet::http::verb::get, "/retained",
            [capture](auto&, auto& response) { response.body() = "retained DSO route"; });
        capture.reset();
        if (!candidate->start_server("127.0.0.1", 0, 2)) {
            throw std::runtime_error("HTTP ownership fixture could not start native listener");
        }
        auto state = ark::http_server_startup_test_access::state(*candidate);
        retained_state = state;
        retained_native = state->active->server.get();
        state.reset();
        candidate.reset();
        throw std::runtime_error("controlled HTTP ownership install failure");
    }

    void uninstall() override {
        std::fputs("forbidden HTTP ownership fixture uninstall\n", stderr);
    }
};

} // namespace

ARK_EXPORT_FUNC void DllEntryPlugin(ark::plugin_manager* manager, const std::string& name, ark::logger* logger) {
    ark::logger::init(logger);
    manager->register_plugin<http_ownership_install_failure>(name);
}

ARK_EXPORT_FUNC bool http_ownership_fixture_retained() {
    return !retained_state.expired() && !retained_capture.expired() && retained_native &&
           retained_native->is_started() && retained_native->iopool().started() &&
           retained_native->iopool().get(0)->get_thread_id() != std::thread::id{};
}
