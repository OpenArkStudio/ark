// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <memory>

#include <ark/module.hpp>

namespace construction_test {

struct counts {
    unsigned destroyed{};
    unsigned updates{};
};

class service : public ark::module {
public:
    virtual service* lookup_self() = 0;
};

std::unique_ptr<service> make_service(counts& events);

} // namespace construction_test
