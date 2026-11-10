// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <ark/module_base.hpp>
#include "module_factory.hpp"

namespace construction_test {
namespace {

class service_impl final : public ark::module_base<service> {
public:
    explicit service_impl(counts& events)
        : events_(events) {
    }

    ~service_impl() override {
        ++events_.destroyed;
    }

    bool update() override {
        ++events_.updates;
        return true;
    }

    service* lookup_self() override {
        return find_module<service>();
    }

private:
    counts& events_;
};

} // namespace

std::unique_ptr<service> make_service(counts& events) {
    return std::make_unique<service_impl>(events);
}

} // namespace construction_test
