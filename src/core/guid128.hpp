/*
 * This source file is part of ArkNX
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#pragma once

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>

namespace ark {

class guid_t128 {
public:
    std::uint64_t high;
    std::uint64_t low;

    guid_t128() = default;

    guid_t128(std::uint64_t value)
        : high(0)
        , low(value) {
    }

    guid_t128(std::uint64_t high, std::uint64_t low)
        : high(high)
        , low(low) {
    }

    bool is_null() {
        return (0 == high) && (0 == low);
    }

    bool is_null() const {
        return (0 == high) && (0 == low);
    }

    guid_t128& operator=(const int& rhs) {
        high = 0;
        low = rhs;
        return *this;
    }

    guid_t128& operator=(const std::int64_t& rhs) {
        high = 0;
        low = rhs;
        return *this;
    }

    guid_t128& operator=(const guid_t128& rhs) {
        if (this != &rhs) {
            high = rhs.high;
            low = rhs.low;
        }
        return *this;
    }

    bool operator==(const guid_t128& rhs) const {
        return (this->high == rhs.high) && (this->low == rhs.low);
    }

    bool operator!=(const guid_t128& rhs) const {
        return !(*this == rhs);
    }

    bool operator<(const guid_t128& rhs) const {
        if (this->high == rhs.high) {
            return this->low < rhs.low;
        }

        return this->high < rhs.high;
    }

    std::string to_string() const {
        return std::to_string(high) + "-" + std::to_string(low);
    }

    bool from_string(const std::string& str_id) {
        const auto separator = str_id.find('-');
        if (separator == std::string::npos || separator == 0 || separator + 1 == str_id.size()) {
            return false;
        }

        const auto* begin = str_id.data();
        const auto* middle = begin + separator;
        const auto* end = begin + str_id.size();
        std::uint64_t parsed_high;
        std::uint64_t parsed_low;
        const auto high_result = std::from_chars(begin, middle, parsed_high);
        if (high_result.ec != std::errc{} || high_result.ptr != middle) {
            return false;
        }

        const auto low_result = std::from_chars(middle + 1, end, parsed_low);
        if (low_result.ec != std::errc{} || low_result.ptr != end) {
            return false;
        }

        high = parsed_high;
        low = parsed_low;
        return true;
    }
};

} // namespace ark
