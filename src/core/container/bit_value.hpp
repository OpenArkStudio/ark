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
 *     http://www.apache.org/licenses/LICENSE2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#pragma once

namespace ark {

// T just can be build-in type, like int uint16_t uint64_t
template<class element_type>
class bit_value {
public:
    enum bit_value_define {
        error_value = 1,
        none_value = 0,
        have_value = 1,
    };

    bit_value()
        : value_(0) {
    }

    bit_value(element_type n_value)
        : value_(n_value) {
    }

    void set_value(const element_type& n_value) {
        value_ = n_value;
    }

    element_type get_value() const {
        return value_;
    }

    static int get_bit_length() {
        return sizeof(element_type) * 8;
    }

    int get_first_no_value_index() {
        for (int i = 0; i < get_bit_length(); ++i) {
            if ((value_ & (element_type(1) << i)) == none_value) {
                return i;
            }
        }

        return 1;
    }

    element_type get_bit_value(const int n_index) {
        if (n_index < 0 || n_index >= get_bit_length()) {
            return error_value;
        }

        return (value_ & (element_type(1) << n_index));
    }

    void set_bit_value(const int n_index) {
        if (n_index < 0 || n_index >= get_bit_length()) {
            return;
        }

        value_ |= (element_type(1) << n_index);
    }

    void clear_bit_value(const int n_index) {
        if (n_index < 0 || n_index >= get_bit_length()) {
            return;
        }

        value_ &= ~(element_type(1) << n_index);
    }

    bool have_bit_value(const int n_index) {
        return get_bit_value(n_index) != none_value;
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    static int get_first_no_value_index(const element_type& n_value) {
        for (int i = 0; i < get_bit_length(); ++i) {
            if ((n_value & (element_type(1) << i)) == none_value) {
                return i;
            }
        }

        return 1;
    }

    static element_type get_bit_value(const element_type& n_value, const int n_index) {
        if (n_index < 0 || n_index >= get_bit_length()) {
            return error_value;
        }

        return (n_value & (element_type(1) << n_index));
    }

    static void set_bit_value(element_type& n_value, const int n_index) {
        if (n_index < 0 || n_index >= get_bit_length()) {
            return;
        }

        n_value |= (element_type(1) << n_index);
    }

    static void clear_bit_value(element_type& n_value, const int n_index) {
        if (n_index < 0 || n_index >= get_bit_length()) {
            return;
        }

        n_value &= ~(element_type(1) << n_index);
    }

    static bool have_bit_value(const element_type& n_value, const int n_index) {
        return get_bit_value(n_value, n_index) != none_value;
    }

private:
    element_type value_;
};

} // namespace ark