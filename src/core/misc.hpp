/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
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

#include <iterator>

#include "ark/bus.hpp"
#include "ark/enum.hpp"
#include "ark/macros.hpp"
#include "ark/string_utils.hpp"
#include "ark/time/date_time.hpp"

namespace ark {

class misc {
public:
    static uint32_t get_nearest2_n(uint32_t size) {
        if (size == 0) {
            return 0;
        }

        if (size <= 8) {
            return 8;
        }

        if (size > (1 << 16)) {
            return 0;
        }

        if ((size & (size - 1)) == 0) {
            return size;
        }

        int count = 0;

        while (size) {
            size = size >> 1;
            ++count;
        }

        return 1 << count;
    }

    template<typename element_type>
    static std::string to_string(const element_type& value) {
        return ARK_LEXICAL_CAST<std::string>(value);
    }

    template<typename element_type>
    static bool from_string(const std::string& str_value, element_type& result) {
        try {
            result = ARK_LEXICAL_CAST<element_type>(str_value);
            return true;
        } catch (std::system_error& ex) {
            ARK_ASSERT_NO_EFFECT(0);
            return false;
        }

        return false;
    }

    template<typename element_type>
    static element_type from_string(const std::string& str_value) {
        try {
            element_type result = ARK_LEXICAL_CAST<element_type>(str_value);
            return result;
        } catch ([[maybe_unused]] std::system_error& ex) {
            ARK_ASSERT_NO_EFFECT(0);
            return element_type();
        }
    }

    static std::string bus2_str(bus_id_t bus_id) {
        bus_addr bus_addr(bus_id);
        return bus_addr.to_string();
    }

    static data_type covert_data_type(const std::string& type_name) {
        static char const* data_type_names[] = {
            "bool", "int", "uint", "int64", "uint64", "float", "double", "string", "vector3d", "struct", "table",
            "array",
            // If there are more types, please add type name in here.
        };

        static int data_type_len = std::size(data_type_names);
        for (int i = 0; i < data_type_len; ++i) {
            if (type_name == data_type_names[i]) {
                return data_type(i);
            }
        }

        return data_type::dt_empty;
    }

    template<typename element_type>
    static element_type operate(data_op_type op, const element_type& base, const element_type& delta) {
        switch (op) {
        case data_op_type::dot_inc:
            base += delta;
            break;
        case data_op_type::dot_dec:
            base -= std::min(base, delta);
            break;
        case data_op_type::dot_mul:
            base *= delta;
            break;
        case data_op_type::dot_div:
            ARK_ASSERT_RET_VAL(delta != 0, base);
            base /= delta;
            break;
        case data_op_type::dot_set:
            base = delta;
            break;
        case data_op_type::dot_and:
            base &= delta;
            break;
        case data_op_type::dot_or:
            base |= delta;
            break;
        default:
            ARK_ASSERT_NO_EFFECT(0);
            break;
        }

        return base;
    }

    /**
     * StrToData
     */
    static long pow_i(int d, int n) {
        int i;
        long m = 1;
        for (i = 0; i < n; i++) {
            m *= d;
        }

        return m;
    }

    static bool is_right_data(char c, int jz) {
        if ((jz < 2) || (jz > 16)) {
            return false;
        }
        char ch[] = "00112233445566778899aAbBcCdDeEfF";
        int i = 0;
        for (i = 0; i < jz; i++) {
            if (c == ch[i * 2]) {
                return true;
            }
            if (c == ch[i * 2 + 1]) {
                return true;
            }
        }
        return false;
    }

    static long str_to_data(const std::string& str_data, int jz) {
        int l = (int)str_data.length();
        long int_value = 0;
        int bit_value = 0;
        int i;
        for (i = 0; i < l; i++) {
            if (is_right_data(str_data[i], jz)) {
                bit_value = to_hex_digit(str_data[i]);
                int_value = bit_value * pow_i(jz, l - i - 1) + int_value;
            } else {
                return -1;
            }
        }
        return int_value;
    }

    static int to_hex_digit(char c) {
        assert(is_right_data(c, 16));

        int bit_value = 0;
        if (isdigit(c)) {
            bit_value = c - 0x30;
        } else {
            switch (c) {
            case 'a':
            case 'A':
                bit_value = 0x0a;
                break;

            case 'b':
            case 'B':
                bit_value = 0x0b;
                break;

            case 'c':
            case 'C':
                bit_value = 0x0c;
                break;

            case 'd':
            case 'D':
                bit_value = 0x0d;
                break;

            case 'e':
            case 'E':
                bit_value = 0x0e;
                break;

            case 'f':
            case 'F':
                bit_value = 0x0f;
                break;
            default:
                break;
            }
        }
        return bit_value;
    }

    /**
     * ConvertToCStyleStr
     * 
     * 00 01 02 03 04 05 06 07  ........
     * 08 09 0a 0b 0c 0d 0e 0f  ........
     * 10 11 12 13 14 15 16 17  ........
     * 18 19 1a 1b 1c 1d 1e 1f  ........
     * 20 21 22 23 24 25 26 27   !"#$%&'
     * 28 29 2a 2b 2c 2d 2e 2f  ()*+,-./
     * 30 31 32 33 34 35 36 37  01234567
     * 38 39 3a 3b 3c 3d 3e 3f  89:;<=>?
     * 40 41 42 43 44 45 46 47  @ABCDEFG
     * 48 49 4a 4b 4c 4d 4e 4f  HIJKLMNO
     * 50 51 52 53 54 55 56 57  PQRSTUVW
     * 58 59 5a 5b 5c 5d 5e 5f  XYZ[\]^_
     * 60 61 62 63 64 65 66 67  `abcdefg
     * 68 69 6a 6b 6c 6d 6e 6f  hijklmno
     * 70 71 72 73 74 75 76 77  pqrstuvw
     * 78 79 7a 7b 7c 7d 7e 7f  xyz{|}~.
     */

    static std::string convert_to_c_style_str(const std::string& input) {
        static const char* shift_map[] = {"\\(00)", "\\(01)", "\\(02)", "\\(03)", "\\(04)", "\\(05)", "\\(06)",
            "\\(07)", "\\(08)", "\\t", "\\n", "\\(0B)", "\\(0C)", "\\r", "\\(0E)", "\\(0F)", "\\(10)", "\\(11)",
            "\\(12)", "\\(13)", "\\(14)", "\\(15)", "\\(16)", "\\(17)", "\\(18)", "\\(19)", "\\(1A)", "\\(1B)",
            "\\(1C)", "\\(1D)", "\\(1E)", "\\(1F)", " ", "!", "\"", "#", "$", "%", "&", "\'", "(", ")", "*", "+", ",",
            "-", ".", "/", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", "=", ">", "?", "@", "A",
            "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
            "W", "X", "Y", "Z", "[", "\\\\", "]", "^", "_", "`", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k",
            "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "{", "|", "}", "~", "\\(7F)"};

        std::string rst_string;
        // Pre allocated memory
        rst_string.reserve(input.size() * 2);

        for (size_t i = 0; i < input.size(); i++) {
            //bool isAscIIChar = int8_t(_Str[i]) >= 0x0 && int8_t(_Str[i]) <= 0x7f;
            bool is_asc_ii_char = (static_cast<unsigned char>(input[i]) <= 127);
            if (is_asc_ii_char) {
                // english characters
                rst_string.append(shift_map[size_t(input[i])]);
            } else {
                // other characters
                rst_string.append(1, input[i]);
            }
        }
        return rst_string;
    }

    static std::string hexdump_oneline(const std::string& prefix, const std::string& line, size_t width) {
        std::ostringstream ostrm;
        ostrm << prefix;

        // hex
        for (size_t i = 0; i < line.size(); i++) {
            char hex_text[16] = "";
            snprintf(hex_text, sizeof(hex_text), "%02X ", (unsigned char)(line[i]));
            ostrm << hex_text;
            if (i == 7 || i == 15 || i == 23) {
                ostrm << ' ';
            }
        }

        // the space between hex and text
        for (unsigned long spacer = line.size(); spacer < width; spacer++) {
            ostrm << "   ";
        }
        if (line.size() <= 8) {
            ostrm << "   ";
        } else if (line.size() <= 16) {
            ostrm << "  ";
        } else if (line.size() <= 24) {
            ostrm << " ";
        }
        ostrm << ": ";

        // text
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] < 32) {
                ostrm << '.';
            } else {
                ostrm << char(line[i]);
            }
        }

        ostrm << std::endl;

        return ostrm.str();
    }

    static std::string to_hex_dump_text(const std::string& input, size_t width, const std::string& prefix = "| ") {
        std::ostringstream ostrm;

        std::string str = input;
        while (str.size() > width) {
            std::string line(str.c_str(), width);
            ostrm << hexdump_oneline(prefix, line, width);
            str = str.substr(width);
        }

        if (str.size() > 0) {
            ostrm << hexdump_oneline(prefix, str, width);
        }

        return ostrm.str();
    }

    /**
     * ConvertFromCStyleStr
     */
    static bool is_hex_number(const char& c) {
        const static char hex_number[] = "0123456789abcdefABCDEF";
        return (strchr(hex_number, c) != NULL);
    }

    // pSrc is the string that format is "\(3F)".
    static int proc_shift_char_by_num(char& dst, const char* const p_src, int src_str_len) {
        assert(p_src != NULL);

        // check if it's the standard format : "\(3F)"
        bool is_allowd_format = false;
        if (src_str_len < 5) {
            is_allowd_format = false;
        } else if (p_src[0] != '\\' || p_src[1] != '(' || p_src[4] != ')') {
            is_allowd_format = false;
        } else if ((!is_hex_number(p_src[2])) || (!is_hex_number(p_src[3]))) {
            is_allowd_format = false;
        } else {
            is_allowd_format = true;
        }

        if (is_allowd_format == false) {
            // illegal format.
            //assert(false); // tag: do not assert

            //For unrecognized escapes, the '\' symbol is parsed as a non escape indicator and
            // the cursor points to the next character of the '\' symbol
            dst = p_src[0];
            return 1;
        }

        // Right situation
        std::string hex_num_string(&p_src[2], 2);
        int hex_num = str_to_data(hex_num_string, 16);
        dst = (static_cast<unsigned char>(hex_num));
        return 5; // standard format:"\(3F)" is 5 bytes.
    }

    // pSrc is "\r" "\n" "\t" "\\"
    static int proc_shift_char_by_char(char& dst, const char* const p_src, int src_str_len) {
        // Check format.
        if (src_str_len < 2) {
            // illegal format.
            //assert(false); // tag: do not assert

            // For unrecognized escapes, the '\' symbol is parsed as a non escape indicator and
            // the cursor points to the next character of the '\' symbol
            dst = p_src[0];
            return 1;
        }

        switch (p_src[1]) {
        case 'r':
            dst = '\r';
            return 2;
        case 'n':
            dst = '\n';
            return 2;
        case 't':
            dst = '	';
            return 2;
        case '\\':
            dst = '\\';
            return 2;
        default:
            // illegal format.
            //assert(false); // tag: do not assert

            //For unrecognized escapes, the '\' symbol is parsed as a non escape indicator and
            // the cursor points to the next character of the '\' symbol
            dst = p_src[0];
            return 1;
        }
    }

    // return the pos that the iter moves.
    static int proc_shift_char(char& dst, const char* p_src, int src_str_len) {
        switch (p_src[1]) {
        case 'r':
        case 'n':
        case 't':
        case '\\':
            return proc_shift_char_by_char(dst, p_src, src_str_len);
        case '(':
            return proc_shift_char_by_num(dst, p_src, src_str_len);
        default:
            // illegal format.
            //assert(false); // tag: do not assert

            //For unrecognized escapes, the '\' symbol is parsed as a non escape indicator and
            // the cursor points to the next character of the '\' symbol
            dst = p_src[0];
            return 1;
        }
    }

    static std::string convert_from_c_style_str(const std::string& c_style_string) {
        std::string rst_string;
        // Pre allocated memory
        rst_string.reserve(c_style_string.size());

        size_t c_style_str_index = 0;
        while (c_style_str_index < c_style_string.size()) {
            if (c_style_string[c_style_str_index] != '\\') {
                rst_string.append(1, c_style_string[c_style_str_index]);
                c_style_str_index++;
            } else {
                char shift_char = 0;
                int c_style_str_left_len = c_style_string.size() - c_style_str_index;
                c_style_str_index +=
                    proc_shift_char(shift_char, &c_style_string[c_style_str_index], c_style_str_left_len);
                rst_string.append(1, shift_char);
            }
        }

        return rst_string;
    }

    // when len <= prefixLen
    //   test_str(adfadf, 3) == adf
    // when len < 20
    //   test_str(adf, 13) == adfa:67890123
    // when len >= 20
    //   test_str(asdf, 45) = asdf:=====(20_56789)(30_56789)12345
    static std::string gen_test_str(const std::string& str_prefix, const size_t str_len) {
        std::ostringstream ostr;
        ostr << str_prefix << ':';
        std::string str = ostr.str();

        // when len <= prefixLen
        //   test_str(adfadf, 3) == adf
        if (str.size() >= str_len) {
            str.resize(str_len);
            return str;
        }

        // when len < 20
        //   test_str(adf, 13) == adfa:67890123
        if (str_len < 20) {
            for (size_t i = str.size() + 1; i <= str_len; ++i) {
                str.push_back(char(((i % 10) + 0x30)));
            }

            return str;
        }

        // when len >= 20
        //   test_str(asdf, 45) = asdf:=====(20_56789)(30_56789)12345
        {
            // asdf:=====
            str.resize(10, '=');

            // (20_56789)(30_56789)
            while (str.size() + 10 <= str_len) {
                const size_t new_len = str.size() + 10;
                std::ostringstream tmp_ostrm;
                tmp_ostrm << str << "(" << new_len << '_';
                str = tmp_ostrm.str();
                for (size_t i = str.size() + 1; i < new_len; ++i) {
                    str.push_back(char(((i % 10) + 0x30)));
                }
                str += ')';
            }

            // 12345
            if (str.size() < str_len) {
                for (size_t i = str.size() + 1; i <= str_len; ++i) {
                    char c = ((i % 10) + 0x30);
                    str.push_back(c);
                }
            }

            return str;
        }

        return str;
    }

    static std::string make_test_str(size_t test_str_size) {
        static const std::string start_str = "!T_";
        static const std::string interval_str = "_T";

        std::ostringstream ostr;
        ostr << start_str << date_time::get_now_time();
        std::string msg_str = ostr.str();
        msg_str += gen_test_str(interval_str, test_str_size - msg_str.size());
        return msg_str;
    }

    static std::string random_string(size_t length) {
        auto randchar = []() -> char {
            const char charset[] = "0123456789"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "abcdefghijklmnopqrstuvwxyz";
            const size_t max_index = (sizeof(charset) - 1);
            return charset[rand() % max_index];
        };
        std::string str(length, 0);
        std::generate_n(str.begin(), length, randchar);
        return str;
    }
};

} // namespace ark
