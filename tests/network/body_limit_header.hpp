// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include "ark/net/net_msg.hpp"

namespace ark::test {

template<class base>
class large_body_header : public base
{
public:
    std::uint32_t max_body_size() const override {
        return 8192;
    }

    msg_header* create() const override {
        return new large_body_header;
    }

    msg_header* clone() const override {
        return new large_body_header(*this);
    }
};

} // namespace ark::test
