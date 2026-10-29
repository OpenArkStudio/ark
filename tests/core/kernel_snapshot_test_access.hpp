// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include "ark/kernel/table_impl.hpp"

namespace ark {

class kernel_nested_snapshot_test_access
{
public:
    static std::uint64_t row_counter(const table_impl& table) {
        return table.last_incarnation_;
    }

    static void row_counter(table_impl& table, std::uint64_t value) {
        table.last_incarnation_ = value;
    }
};

} // namespace ark
