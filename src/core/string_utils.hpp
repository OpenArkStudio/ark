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

#include "ark/macros.hpp"
#include "ark/platform.hpp"

namespace ark {

namespace detail {

inline void dbg(const char* s, std::string& fs) {
    //fs << '\"' << s << '\"';
    fs = ARK_FORMAT("{}{}{}{}", fs, '\"', s, '\"');
}

inline void dbg(const std::string& s, std::string& fs) {
    //fs << '\"' << s << '\"';
    fs = ARK_FORMAT("{}{}{}{}", fs, '\"', s, '\"');
}

template<typename element_type>
inline void dbg(const element_type& t, std::string& fs) {
    //fs << t;
    fs = ARK_FORMAT("{}{}", fs, t);
}

template<typename key_type, typename mapped_type>
inline void dbg(const std::pair<key_type, mapped_type>& x, std::string& fs) {
    dbg(x.first, fs);
    //fs << ':';
    fs = ARK_FORMAT("{}{}", fs, ':');
    dbg(x.second, fs);
}

template<typename element_type>
void dbg(const element_type& beg, const element_type& end, char c1, char c2, std::string& fs) {
    if (beg == end)
    {
        //fs << c1 << c2;
        fs = ARK_FORMAT("{}{}{}", fs, c1, c2);
        return;
    }

    //fs << c1;
    fs = ARK_FORMAT("{}{}", fs, c1);
    for (element_type it = beg; it != end; ++it)
    {
        dbg(*it, fs);
        //fs << ',';
        fs = ARK_FORMAT("{}{}", fs, ',');
    }

    fs.back() = c2;
}

} // namespace detail

class string_utils
{
public:
    // split("x y z", ' ');  ->  [ "x", "y", "z" ]
    // split("|x|y|", '|');  ->  [ "", "x", "y" ]
    // split("xooy", "oo");  ->  [ "x", "y"]
    // split("xooy", 'o', 1);  ->  [ "x", "oy" ]
    static inline void split(std::vector<std::string>& v, const char* s, char c, uint32_t max_split = 0) {
        const char* p;
        const char* from = s;

        while ((p = strchr(from, c)))
        {
            v.emplace_back(std::string_view(from, p - from));
            from = p + 1;
            if (v.size() == max_split)
            {
                break;
            }
        }

        if (from < s + strlen(s))
        {
            v.emplace_back(std::string_view(from));
        }
    }

    static inline void split(std::vector<std::string>& v, const std::string& s, char c, uint32_t max_split = 0) {
        const char* p;
        const char* from = s.data();
        const char* end = from + s.size();

        while ((p = static_cast<const char*>(memchr(from, c, end - from))))
        {
            v.emplace_back(std::string_view(from, p - from));
            from = p + 1;
            if (v.size() == max_split)
            {
                break;
            }
        }

        if (from < end)
        {
            v.emplace_back(std::string_view(from, end - from));
        }
    }

    static inline void split(std::vector<std::string>& v, const char* s, const char* c, uint32_t max_split = 0) {
        const char* p;
        const char* from = s;
        size_t n = strlen(c);

        // An empty delimiter cannot advance the input cursor.
        if (n == 0)
        {
            if (*s != '\0')
            {
                v.emplace_back(s);
            }
            return;
        }

        while ((p = strstr(from, c)))
        {
            v.emplace_back(std::string(from, p - from));
            from = p + n;
            if (v.size() == max_split)
            {
                break;
            }
        }

        if (from < s + strlen(s))
        {
            v.emplace_back(std::string_view(from));
        }
    }

    static inline void split(std::vector<std::string>& v, const std::string& s, const char* c, uint32_t max_split = 0) {
        split(v, s.c_str(), c, max_split);
    }

    // replace("xooxoox", "oo", "ee");     ->  "xeexeex"
    // replace("xooxoox", "oo", "ee", 1);  ->  "xeexoox"
    static inline auto replace(
        const char* s, const char* sub, const char* to, uint32_t max_replace = 0) -> std::string {
        const char* p;
        const char* from = s;
        size_t n = strlen(sub);
        if (n == 0)
        {
            return s;
        }
        size_t m = strlen(to);

        std::string x;

        while ((p = strstr(from, sub)))
        {
            x.append(from, p - from);
            x.append(to, m);
            from = p + n;
            if (--max_replace == 0)
            {
                break;
            }
        }

        if (from < s + strlen(s))
        {
            x.append(from);
        }

        return x;
    }

    static inline auto replace(
        const std::string& s, const char* sub, const char* to, uint32_t max_replace = 0) -> std::string {
        if (*sub == '\0')
        {
            return s;
        }
        const char* from = s.c_str();
        const char* p = strstr(from, sub);
        if (p == nullptr)
        {
            return s;
        }

        size_t n = strlen(sub);
        size_t m = strlen(to);
        std::string x;
        x.reserve(s.size());

        do
        {
            x.append(from, p - from).append(to, m);
            from = p + n;
            if (--max_replace == 0)
            {
                break;
            }
        } while ((p = strstr(from, sub)));

        if (from < s.data() + s.size())
        {
            x.append(from);
        }

        return x;
    }

    // strip("abxxa", "ab")       ->  "xx"     strip both left and right.
    // strip("abxxa", "ab", 'l')  ->  "xxa"    strip left only.
    // strip("abxxa", "ab", 'r')  ->  "abxx"   strip right only.
    static inline auto strip(const char* s, const char* c = " \t\r\n", char d = 'b') -> std::string {
        if (!*s)
        {
            return std::string();
        }

        std::array<char, 256> bs = {0};
        while (*c)
        {
            bs[static_cast<uint8_t>(*c++)] = 1;
        }

        if (d == 'l' || d == 'L')
        {
            while (bs[static_cast<uint8_t>(*s)])
            {
                ++s;
            }
            return std::string(s);
        }
        else if (d == 'r' || d == 'R')
        {
            const char* e = s + strlen(s) - 1;
            while (e >= s && bs[static_cast<uint8_t>(*e)])
            {
                --e;
            }

            return std::string(s, e + 1 - s);
        }
        else
        {
            while (bs[static_cast<uint8_t>(*s)])
            {
                ++s;
            }

            const char* e = s + strlen(s) - 1;
            while (e >= s && bs[static_cast<uint8_t>(*e)])
            {
                --e;
            }

            return std::string(s, e + 1 - s);
        }
    }

    static inline auto strip(const char* s, char c, char d = 'b') -> std::string {
        if (!*s)
        {
            return std::string();
        }

        if (d == 'l' || d == 'L')
        {
            while (*s == c)
            {
                ++s;
            }

            return std::string(s);
        }
        else if (d == 'r' || d == 'R')
        {
            const char* e = s + strlen(s) - 1;
            while (e >= s && *e == c)
            {
                --e;
            }
            return std::string(s, e + 1 - s);
        }
        else
        {
            while (*s == c)
            {
                ++s;
            }
            const char* e = s + strlen(s) - 1;
            while (e >= s && *e == c)
            {
                --e;
            }
            return std::string(s, e + 1 - s);
        }
    }

    static inline auto strip(const std::string& s, const char* c = " \t\r\n", char d = 'b') -> std::string {
        if (s.empty())
        {
            return std::string();
        }

        std::array<char, 256> bs = {0};
        while (*c)
        {
            bs[static_cast<uint8_t>(*c++)] = 1;
        }

        if (d == 'l' || d == 'L')
        {
            size_t b = 0;
            while (b < s.size() && bs[static_cast<uint8_t>(s[b])])
            {
                ++b;
            }
            return b == 0 ? s : s.substr(b);
        }
        else if (d == 'r' || d == 'R')
        {
            size_t e = s.size();
            while (e > 0 && bs[static_cast<uint8_t>(s[e - 1])])
            {
                --e;
            }
            return e == s.size() ? s : s.substr(0, e);
        }
        else
        {
            size_t b = 0, e = s.size();
            while (b < s.size() && bs[static_cast<uint8_t>(s[b])])
            {
                ++b;
            }
            if (b == s.size())
            {
                return std::string();
            }
            while (e > 0 && bs[static_cast<uint8_t>(s[e - 1])])
            {
                --e;
            }
            return (e - b == s.size()) ? s : s.substr(b, e - b);
        }
    }

    static inline auto strip(const std::string& s, char c, char d = 'b') -> std::string {
        if (s.empty())
        {
            return std::string();
        }

        if (d == 'l' || d == 'L')
        {
            size_t b = 0;
            while (b < s.size() && s[b] == c)
            {
                ++b;
            }
            return b == 0 ? s : s.substr(b);
        }
        else if (d == 'r' || d == 'R')
        {
            size_t e = s.size();
            while (e > 0 && s[e - 1] == c)
            {
                --e;
            }
            return e == s.size() ? s : s.substr(0, e);
        }
        else
        {
            size_t b = 0, e = s.size();
            while (b < s.size() && s[b] == c)
            {
                ++b;
            }
            if (b == s.size())
            {
                return std::string();
            }
            while (e > 0 && s[e - 1] == c)
            {
                --e;
            }
            return (e - b == s.size()) ? s : s.substr(b, e - b);
        }
    }

    static inline auto strip(const std::string& s, const std::string& c, char d = 'b') -> std::string {
        if (s.empty())
        {
            return std::string();
        }

        std::array<char, 256> bs = {0};
        for (char i : c)
        {
            bs[static_cast<uint8_t>(i)] = 1;
        }

        if (d == 'l' || d == 'L')
        {
            size_t b = 0;
            while (b < s.size() && bs[static_cast<uint8_t>(s[b])])
            {
                ++b;
            }
            return b == 0 ? s : s.substr(b);
        }
        else if (d == 'r' || d == 'R')
        {
            size_t e = s.size();
            while (e > 0 && bs[static_cast<uint8_t>(s[e - 1])])
            {
                --e;
            }
            return e == s.size() ? s : s.substr(0, e);
        }
        else
        {
            size_t b = 0, e = s.size();
            while (b < s.size() && bs[static_cast<uint8_t>(s[b])])
            {
                ++b;
            }
            if (b == s.size())
            {
                return std::string();
            }

            while (e > 0 && bs[static_cast<uint8_t>(s[e - 1])])
            {
                --e;
            }
            return (e - b == s.size()) ? s : s.substr(b, e - b);
        }
    }

    static inline auto to_lower(std::string& str) {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return tolower(c); });
    }

    static inline auto to_lower(std::wstring& str) {
        std::transform(str.begin(), str.end(), str.begin(), [](wchar_t c) { return towlower(c); });
    }

    template<typename key_type, typename mapped_type>
    inline auto dbg(const std::pair<key_type, mapped_type>& x) -> std::string {
        std::string fs;
        fs.reserve(64);
        detail::dbg(x, fs);
        return fs;
    }

    template<typename element_type>
    inline auto dbg(const element_type& beg, const element_type& end, char c1, char c2) -> std::string {
        std::string fs;
        fs.reserve(128);
        detail::dbg(beg, end, c1, c2, fs);
        return fs;
    }

    template<typename element_type>
    inline auto dbg(const std::vector<element_type>& v) -> std::string {
        return dbg(v.begin(), v.end(), '[', ']');
    }

    template<typename element_type>
    inline auto dbg(const std::set<element_type>& v) -> std::string {
        return dbg(v.begin(), v.end(), '{', '}');
    }

    template<typename key_type, typename mapped_type>
    inline auto dbg(const std::map<key_type, mapped_type>& v) -> std::string {
        return dbg(v.begin(), v.end(), '{', '}');
    }

    template<typename element_type>
    inline auto dbg(const std::unordered_set<element_type>& v) -> std::string {
        return dbg(v.begin(), v.end(), '{', '}');
    }

    template<typename key_type, typename mapped_type>
    inline auto dbg(const std::unordered_map<key_type, mapped_type>& v) -> std::string {
        return dbg(v.begin(), v.end(), '{', '}');
    }
};

} // namespace ark

//namespace {
//// internal functions
//struct NoCaseCompareChar
//{
//    bool operator()(char l, char r) const
//    {
//        bool bEqual = (l == r);
//        if (bEqual)
//        {
//            return true;
//        }
//        if (isalpha(static_cast<unsigned char>(l)))
//        {
//            if (isupper(static_cast<unsigned char>(l)))
//            {
//                return l == toupper(r);
//            }
//            else
//            {
//                return l == tolower(r);
//            }
//        }
//
//        return bEqual;
//    }
//    bool operator()(wchar_t l, wchar_t r) const
//    {
//        bool bEqual = (l == r);
//
//        if (bEqual)
//        {
//            return true;
//        }
//
//        if (iswalpha(l))
//        {
//            if (iswupper(l))
//            {
//                return l == static_cast<wchar_t>(towupper(r));
//            }
//            else
//            {
//                return l == static_cast<wchar_t>(towlower(r));
//            }
//        }
//
//        return bEqual;
//    }
//} no_case_compare_char;
//
//template<typename T>
//inline bool stringUtil_StartsWith(const T& str, const T& pattern, bool case_sensitive)
//{
//    // H_ASSERT( str.length() >= pattern.length() );
//    if (str.length() < pattern.length())
//    {
//        return false;
//    }
//
//    if (case_sensitive)
//    {
//        return std::equal(pattern.begin(), pattern.end(), str.begin());
//    }
//    else
//    {
//        return std::equal(pattern.begin(), pattern.end(), str.begin(), no_case_compare_char);
//    }
//}
//
//template<typename T>
//bool stringUtil_EndsWith(const T& str, const T& pattern, bool case_sensitive)
//{
//    // H_ASSERT( str.length() >= pattern.length() );
//    if (str.length() < pattern.length())
//    {
//        return false;
//    }
//
//    if (case_sensitive)
//    {
//        return equal(pattern.rbegin(), pattern.rend(), str.rbegin());
//    }
//    else
//    {
//        return equal(pattern.rbegin(), pattern.rend(), str.rbegin(), no_case_compare_char);
//    }
//}
//
//template<typename T>
//struct nocase_equal_char
//{
//    T m_c;
//    nocase_equal_char(T c)
//        : m_c(c)
//    {
//    }
//    bool operator()(T c)
//    {
//        return no_case_compare_char(m_c, c);
//    }
//};
//
//template<typename T>
//inline bool stringUtil_contains(const T& str, typename T::value_type c, bool case_sensitive)
//{
//    if (case_sensitive)
//    {
//        return str.find(c, 0) != T::npos;
//    }
//    else
//    {
//        return str.end() != std::find_if(str.begin(), str.end(), nocase_equal_char<typename T::value_type>(c));
//    }
//}
//template<typename T>
//inline bool stringUtil_contains(const T& strL, const T& strR, bool case_sensitive)
//{
//    if (case_sensitive)
//    {
//        return strL.end() != std::search(strL.begin(), strL.end(), strR.begin(), strR.end());
//    }
//    else
//    {
//        return strL.end() != std::search(strL.begin(), strL.end(), strR.begin(), strR.end(), no_case_compare_char);
//    }
//}
//
//template<typename _StringType>
//inline void stringUtil_trim_charptr(_StringType& str, typename _StringType::const_pointer delims, bool left, bool right)
//{
//    if (str.empty())
//    {
//        return;
//    }
//
//    size_t stop_pos = (size_t)str.size() - 1; // included
//    if (right)
//    {
//        stop_pos = str.find_last_not_of(delims);
//    }
//
//    if (stop_pos == _StringType::npos)
//    {
//        str = _StringType();
//        return;
//    }
//
//    size_t start_pos = 0; // included
//    if (left)
//    {
//        start_pos = str.find_first_not_of(delims);
//    }
//
//    if (start_pos == 0 && stop_pos == str.size() - 1)
//    {
//        return;
//    }
//
//    str = _StringType(str.data() + start_pos, stop_pos + 1 - start_pos);
//}
//
//template<typename _StringType>
//inline void stringUtil_trim_string(_StringType& str, const _StringType& delims, bool left, bool right)
//{
//    if (str.empty())
//    {
//        return;
//    }
//
//    if (str.empty())
//    {
//        return;
//    }
//
//    size_t stop_pos = (size_t)str.size() - 1; // included
//    if (right)
//    {
//        stop_pos = str.find_last_not_of(delims);
//    }
//
//    if (stop_pos == _StringType::npos)
//    {
//        str = _StringType();
//        return;
//    }
//
//    size_t start_pos = 0; // included
//    if (left)
//    {
//        start_pos = str.find_first_not_of(delims);
//    }
//
//    if (start_pos == 0 && stop_pos == str.size() - 1)
//    {
//        return;
//    }
//
//    str = _StringType(str.data() + start_pos, stop_pos + 1 - start_pos);
//}
//
//template<typename _StringVector, typename StringType, typename _DelimType>
//inline void _stringUtilSplit(
//    _StringVector& ret, const StringType& str, const _DelimType& delims, unsigned int maxSplits)
//{
//    unsigned int numSplits = 0;
//
//    // Use STL methods
//    size_t start, pos;
//    start = 0;
//
//    do
//    {
//        pos = str.find_first_of(delims, start);
//
//        if (pos == start)
//        {
//            ret.push_back(StringType());
//            start = pos + 1;
//        }
//        else if (pos == StringType::npos || (maxSplits && numSplits + 1 == maxSplits))
//        {
//            // Copy the rest of the std::string
//            ret.push_back(StringType());
//            *(ret.rbegin()) = StringType(str.data() + start, str.size() - start);
//            break;
//        }
//        else
//        {
//            // Copy up to delimiter
//            // ret.push_back( str.substr( start, pos - start ) );
//            ret.push_back(StringType());
//            *(ret.rbegin()) = StringType(str.data() + start, pos - start);
//            start = pos + 1;
//        }
//
//        ++numSplits;
//
//    } while (pos != StringType::npos);
//}
//
//template<typename _SliceVector, typename StringType, typename _DelimType>
//void _stringUtilSplitStringToSliceVector(
//    _SliceVector& ret, const StringType& str, const _DelimType& delims, unsigned int maxSplits)
//{
//    unsigned int numSplits = 0;
//
//    // Use STL methods
//    size_t start, pos;
//    start = 0;
//
//    do
//    {
//        pos = str.find_first_of(delims, start);
//
//        if (pos == start)
//        {
//            ret.push_back(ark::AFSlice());
//            start = pos + 1;
//        }
//        else if (pos == StringType::npos || (maxSplits && numSplits + 1 == maxSplits))
//        {
//            // Copy the rest of the std::string
//            ret.push_back(ark::AFSlice(str.data() + start, str.size() - start));
//            break;
//        }
//        else
//        {
//            // Copy up to delimiter
//            // ret.push_back( str.substr( start, pos - start ) );
//            ret.push_back(ark::AFSlice(str.data() + start, pos - start));
//            start = pos + 1;
//        }
//
//        ++numSplits;
//
//    } while (pos != StringType::npos);
//}
//
//template<typename StringType, typename _DelimType>
//inline void _stringUtilSplitStringToSlice(
//    const StringType& str, const _DelimType& delims, ark::AFSlice* ret, size_t& slices_count)
//{
//    unsigned int numSplits = 0;
//
//    // Use STL methods
//    size_t start, pos;
//    start = 0;
//
//    do
//    {
//        pos = str.find_first_of(delims, start);
//
//        if (pos == start)
//        {
//            ret[numSplits++] = ark::AFSlice();
//            start = pos + 1;
//        }
//        else if (pos == StringType::npos || ((numSplits + 1) == slices_count))
//        {
//            // Copy the rest of the std::string
//            ret[numSplits++] = (ark::AFSlice(str.data() + start, str.size() - start));
//            break;
//        }
//        else
//        {
//            // Copy up to delimiter
//            // ret.push_back( str.substr( start, pos - start ) );
//            ret[numSplits++] = (ark::AFSlice(str.data() + start, pos - start));
//            start = pos + 1;
//        }
//    } while (pos != StringType::npos);
//
//    slices_count = numSplits;
//}
//
//inline void _stringUtilSplitSliceToSlice(
//    const ark::AFSlice& str, const char& delim, std::vector<ark::AFSlice>& ret, unsigned int maxSplits)
//{
//    // Use STL methods
//    size_t start, pos;
//    start = 0;
//
//    const char* p = NULL;
//    do
//    {
//        // fix strchr compile warning
//#ifdef ARK_PLATFORM_WIN
//        p = (const char*)memchr(start + const_cast<char*>(str.data()), delim, str.size() - start);
//#else
//        p = (const char*)memchr(start + str.data(), delim, str.size() - start);
//#endif
//
//        if (!p || p >= str.data() + str.size() || ((maxSplits) && (ret.size() + 1 == maxSplits)))
//        {
//            ret.push_back(ark::AFSlice(str.data() + start, str.size() - start));
//            break;
//        }
//
//        pos = p - str.data();
//
//        if (pos == start)
//        {
//            ret.push_back(ark::AFSlice());
//            start = pos + 1;
//        }
//        else
//        {
//            ret.push_back(ark::AFSlice(str.data() + start, pos - start));
//            start = pos + 1;
//        }
//    } while (p);
//}
//
//inline void _stringUtilSplitSliceToSlice(
//    const ark::AFSlice& str, const char& delim, ark::AFSlice* ret, size_t& slices_count)
//{
//    unsigned int numSplits = 0;
//
//    // Use STL methods
//    size_t start, pos;
//    start = 0;
//
//    const char* p = NULL;
//    do
//    {
//        // fix strchr compile warning
//#ifdef ARK_PLATFORM_WIN
//        p = (const char*)memchr(start + const_cast<char*>(str.data()), delim, str.size() - start);
//#else
//        p = (const char*)memchr(start + str.data(), delim, str.size() - start);
//#endif
//        if (!p || p >= str.data() + str.size() || (numSplits == slices_count - 1))
//        {
//            ret[numSplits++] = (ark::AFSlice(str.data() + start, str.size() - start));
//            break;
//        }
//
//        pos = p - str.data();
//
//        if (pos == start)
//        {
//            ret[numSplits++] = ark::AFSlice();
//            start = pos + 1;
//        }
//        else
//        {
//            ret[numSplits++] = (ark::AFSlice(str.data() + start, pos - start));
//            start = pos + 1;
//        }
//    } while (p);
//
//    slices_count = numSplits;
//}
//
//template<typename StringType>
//inline void stringUtil_Split(const StringType& src, StringType& left, StringType& right,
//    typename StringType::const_pointer pDelims, size_t nDelimsLength)
//{
//    typename StringType::const_iterator iter = find_first_of(src.begin(), src.end(), pDelims, pDelims + nDelimsLength);
//    if (src.end() == iter)
//    {
//        return;
//    }
//
//    left.assign(src.begin(), iter);
//    iter++;
//    right.assign(iter, src.end());
//}
//
//template<typename _String>
//inline void _replace(_String& str, const _String& needle, const _String& new_value, size_t start_pos /* = 0*/,
//    int replace_count /* = -1*/)
//{
//    if (0 == replace_count)
//    {
//        return;
//    }
//
//    size_t i = 0;
//    size_t pos = str.find(needle, start_pos);
//    while (pos != _String::npos)
//    {
//        str.replace(pos, needle.size(), new_value);
//        pos = str.find(needle, pos);
//        if (++i >= (size_t)(replace_count))
//        {
//            break;
//        }
//    }
//}
//
//inline int php_htoi(const char* s)
//{
//    int value;
//    int c;
//
//    c = ((const unsigned char*)s)[0];
//    if (isupper(c))
//    {
//        c = tolower(c);
//    }
//    value = (c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10) * 16;
//
//    c = ((const unsigned char*)s)[1];
//    if (isupper(c))
//    {
//        c = tolower(c);
//    }
//    value += c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10;
//
//    return (value);
//}
//
//} // namespace
//
//namespace ark {
//
//// Copy from Qihoo360/simcc project
//// @see https://github.com/Qihoo360/simcc/blob/master/simcc/string_util.h
//
//class AFStringUtils
//{
//public:
//    static void Trim(std::string& str, bool left = true, bool right = true)
//    {
//        static const std::string delims("\0 \t\r\n\v", 6);
//        Trim(str, delims, left, right);
//    }
//
//    static void Trim(std::wstring& str, bool left = true, bool right = true)
//    {
//        static const std::wstring delims(L"\0 \t\r\n\v", 6);
//        Trim(str, delims, left, right);
//    }
//
//    static void Trim(std::string& str, const std::string& delims, bool left = true, bool right = true)
//    {
//        stringUtil_trim_string(str, delims, left, right);
//    }
//
//    static void Trim(std::wstring& str, const std::wstring& delims, bool left = true, bool right = true)
//    {
//        stringUtil_trim_string(str, delims, left, right);
//    }
//
//    static void Trim(std::string& str, const char* delims, bool left = true, bool right = true)
//    {
//        stringUtil_trim_charptr(str, delims, left, right);
//    }
//    static void Trim(std::wstring& str, const wchar_t* delims, bool left = true, bool right = true)
//    {
//        stringUtil_trim_charptr(str, delims, left, right);
//    }
//
//    static void TrimLeft(std::string& str)
//    {
//        Trim(str, true, false);
//    }
//
//    static void TrimLeft(std::wstring& str)
//    {
//        Trim(str, true, false);
//    }
//
//    static void TrimLeft(std::string& str, const std::string& delims)
//    {
//        Trim(str, delims, true, false);
//    }
//
//    static void TrimLeft(std::wstring& str, const std::wstring& delims)
//    {
//        Trim(str, delims, true, false);
//    }
//
//    static void TrimLeft(std::string& str, const char* delims)
//    {
//        Trim(str, delims, true, false);
//    }
//
//    static void TrimLeft(std::wstring& str, const wchar_t* delims)
//    {
//        Trim(str, delims, true, false);
//    }
//
//    static void TrimRight(std::string& str)
//    {
//        Trim(str, false, true);
//    }
//
//    static void TrimRight(std::wstring& str)
//    {
//        Trim(str, false, true);
//    }
//
//    static void TrimRight(std::string& str, const std::string& delims)
//    {
//        Trim(str, delims, false, true);
//    }
//
//    static void TrimRight(std::wstring& str, const std::wstring& delims)
//    {
//        Trim(str, delims, false, true);
//    }
//
//    static void TrimRight(std::string& str, const char* delims)
//    {
//        Trim(str, delims, false, true);
//    }
//
//    static void TrimRight(std::wstring& str, const wchar_t* delims)
//    {
//        Trim(str, delims, false, true);
//    }
//
//    // @brief trim a char
//    template<typename _StringType>
//    static void Trim(_StringType& str, char c, bool left = true, bool right = true)
//    {
//        if (str.empty())
//        {
//            return;
//        }
//
//        int stop_pos = (int)str.size() - 1; // included
//        if (right)
//        {
//            for (; stop_pos >= 0; --stop_pos)
//            {
//                if (str[stop_pos] != c)
//                {
//                    break;
//                }
//            }
//        }
//
//        if (stop_pos < 0)
//        {
//            str = _StringType();
//            return;
//        }
//
//        int start_pos = 0; // included
//        if (left)
//        {
//            for (; start_pos <= stop_pos; ++start_pos)
//            {
//                if (str[start_pos] != c)
//                {
//                    break;
//                }
//            }
//        }
//
//        if (start_pos == 0 && stop_pos == (int)str.size() - 1)
//        {
//            return;
//        }
//
//        str = _StringType(str.data() + start_pos, stop_pos + 1 - start_pos);
//    }
//
//    template<typename _StringType>
//    static void TrimLeft(_StringType& str, char c)
//    {
//        Trim(str, c, true, false);
//    }
//
//    template<typename _StringType>
//    static void TrimRight(_StringType& str, char c)
//    {
//        Trim(str, c, false, true);
//    }
//
//    // @brief Replaces a section equaling to <code>needle</code> of the current string with the new substring
//    // <code>new_value</code>
//    // @param string & str [in,out], -
//    // @param const string & needle -
//    // @param const string & new_value -
//    // @param string::size_type start_pos -
//    // @param int replace_count - If it is -1, replaces all the <code>needle</code> by <code>new_value</code>
//    // @return void -
//    static void Replace(std::string& str, const std::string& needle, const std::string& new_value, size_t start_pos = 0,
//        int replace_count = -1)
//    {
//        return _replace(str, needle, new_value, start_pos, replace_count);
//    }
//
//    static void Replace(std::wstring& str, const std::wstring& needle, const std::wstring& new_value,
//        size_t start_pos = 0, int replace_count = -1)
//    {
//        return _replace(str, needle, new_value, start_pos, replace_count);
//    }
//
//    // Returns a StringVector that contains all the substrings delimited
//    // by any of the characters in the passed <code>delims</code> argument.
//    // @param vec[out], the result substrings are stored here.
//    // @param delims A list of delimiter characters to split by
//    // @param max_splits The maximum number of splits to perform (0 for unlimited splits). If this
//    //     parameters is > 0, the splitting process will stop after this many splits, left to right.
//    static void Split(std::vector<std::string>& ret, const std::string& str, const std::string& delims = "\t\n ",
//        unsigned int max_splits = 0)
//    {
//        _stringUtilSplit(ret, str, delims, max_splits);
//    }
//
//    static void Split(std::vector<std::wstring>& ret, const std::wstring& str, const std::wstring& delims = L"\t\n ",
//        unsigned int max_splits = 0)
//    {
//        _stringUtilSplit(ret, str, delims, max_splits);
//    }
//
//    static void Split(std::vector<std::string>& ret, const std::string& str, const std::string::value_type& delims,
//        unsigned int max_splits = 0)
//    {
//        _stringUtilSplit(ret, str, delims, max_splits);
//    }
//
//    static void Split(std::vector<std::wstring>& ret, const std::wstring& str, const std::wstring::value_type& delims,
//        unsigned int max_splits = 0)
//    {
//        _stringUtilSplit(ret, str, delims, max_splits);
//    }
//
//    static void Split(std::vector<AFSlice>& ret, const std::string& str, int delims, unsigned int max_splits = 0)
//    {
//        _stringUtilSplitStringToSliceVector(ret, str, delims, max_splits);
//    }
//
//    static void Split(std::vector<AFSlice>& ret, const std::string& str, const std::string& delims = "\t\n ",
//        unsigned int max_splits = 0)
//    {
//        _stringUtilSplitStringToSliceVector(ret, str, delims, max_splits);
//    }
//
//    // @brief
//    //   <code>
//    //     std::string s = "a|b|c|d|e";
//    //     AFSlice v[2];
//    //     Split(s, '|', v, 2);  //after Split, v[0]=="a", v[1] == "b|c|d|e", vc == 2
//    //   </code>
//    //
//    //   <code>
//    //     std::string s = "a|b|c";
//    //     AFSlice v[8];
//    //     Split(s, '|', v, 8);  //after Split, v[0]=="a", v[1] == "b", v[2] == "c", vc == 3
//    //   </code>
//    // @param const std::string & str -
//    // @param int delims -
//    // @param[out] AFSlice slices[] -
//    // @param[in,out] size_t & slice_count -
//    // @return void -
//    static void Split(const std::string& str, int delims, AFSlice slices[], size_t& slice_count)
//    {
//        _stringUtilSplitStringToSlice(str, delims, slices, slice_count);
//    }
//
//    static void Split(const std::string& str, const std::string& delims, AFSlice slices[], size_t& slice_count)
//    {
//        _stringUtilSplitStringToSlice(str, delims, slices, slice_count);
//    }
//
//    static void Split(const AFSlice& str, int delims, AFSlice slices[], size_t& slice_count)
//    {
//        _stringUtilSplitSliceToSlice(str, delims, slices, slice_count);
//    }
//
//    static void Split(const AFSlice& str, int delims, std::vector<AFSlice>& slices, unsigned int max_splits = 0)
//    {
//        _stringUtilSplitSliceToSlice(str, delims, slices, max_splits);
//    }
//
//    /**
//     * Split a std::string into tow strings using the special characters .
//     * e.g. src="abc, hello world "  if cutset="," then left="abc", right=" hello world "
//     * @warning If any of delimiters was found, we just return, left std::string and right std::string will not be
//     * changed.
//     * @param src The source std::string
//     * @param left The left std::string separated by cutset
//     * @param left The right std::string separated by cutset
//     * @param cutset A list of delimiter characters to split by. We only use the first one when come up against a
//     * delimiter
//     */
//    static void Split(
//        const std::string& src, std::string& left, std::string& right, const std::string& delims = "\t\n ")
//    {
//        Split(src, left, right, delims.c_str());
//    }
//
//    static void Split(
//        const std::wstring& src, std::wstring& left, std::wstring& right, const std::wstring& delims = L"\t\n ")
//    {
//        Split(src, left, right, delims.c_str());
//    }
//
//    static void Split(const std::string& src, std::string& left, std::string& right, const char* delims = "\t\n ")
//    {
//        stringUtil_Split(src, left, right, delims, strlen(delims));
//    }
//
//    static void Split(
//        const std::wstring& src, std::wstring& left, std::wstring& right, const wchar_t* delims = L"\t\n ")
//    {
//        stringUtil_Split(src, left, right, delims, wcslen(delims));
//    }
//
//    template<typename _SourceStringType, typename _SubStringType>
//    static void Explode(const _SourceStringType& source, const _SourceStringType& cutset,
//        std::vector<_SubStringType>& return_value, int limit = -1)
//    {
//        // TODO
//    }

//    static void ToUpper(std::string& str)
//    {
//        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return toupper(c); });
//    }
//
//    static void ToUpper(std::wstring& str)
//    {
//        std::transform(str.begin(), str.end(), str.begin(), [](wchar_t c) { return towupper(c); });
//    }
//
//    // std::string compare
//    // @param case_sensitive true If we compare the std::string with case sensitively
//    static bool Equals(const std::string& str1, const std::string& str2, bool case_sensitive = true)
//    {
//        if (case_sensitive)
//        {
//            return (str1 == str2) ? true : false;
//        }
//        else
//        {
//            return EqualsIgnoreCase(str1, str2);
//        }
//    }
//
//    // @brief std::string compare ignoring case sensitively
//    static bool EqualsIgnoreCase(const std::string& str1, const std::string& str2)
//    {
//        if (str1.length() == str2.length())
//        {
//            return std::equal(str1.begin(), str1.end(), str2.begin(), no_case_compare_char);
//        }
//        return false;
//    }
//
//    // @brief     Returns whether the std::string begins with the pattern passed in.
//    // @param[in] pattern The pattern to compare with.
//    // @param[in] case_sensitive true case sensitive, false ignore the case
//    static bool StartsWith(const std::string& str, const std::string& pattern, bool case_sensitive = true)
//    {
//        return stringUtil_StartsWith(str, pattern, case_sensitive);
//    }
//
//    static bool StartsWith(const std::wstring& str, const std::wstring& pattern, bool case_sensitive = true)
//    {
//        return stringUtil_StartsWith(str, pattern, case_sensitive);
//    }
//
//    // @brief     Returns whether the std::string ends with the pattern passed in.
//    // @param[in] pattern The pattern to compare with.
//    // @param[in] case_sensitive true case sensitive, false ignore the case
//    static bool EndsWith(const std::string& str, const std::string& pattern, bool case_sensitive = true)
//    {
//        return stringUtil_EndsWith(str, pattern, case_sensitive);
//    }
//
//    static bool EndsWith(const std::wstring& str, const std::wstring& pattern, bool case_sensitive = true)
//    {
//        return stringUtil_EndsWith(str, pattern, case_sensitive);
//    }
//
//    // Simple pattern-matching routine allowing a wild card pattern.
//    // @param str String to test
//    // @param pattern Pattern to match against; which can include simple '*' wildcards
//    // @param case_sensitive Whether the match is case sensitive or not
//    static bool Match(const std::string& str, const std::string& pattern, bool case_sensitive = true)
//    {
//        std::string tmpStr = str;
//        std::string tmpPattern = pattern;
//
//        if (!case_sensitive)
//        {
//            AFStringUtils::ToLower(tmpStr);
//            AFStringUtils::ToLower(tmpPattern);
//        }
//
//        std::string::const_iterator strIt = tmpStr.begin();
//        std::string::const_iterator patIt = tmpPattern.begin();
//        std::string::const_iterator lastWildCardIt = tmpPattern.end();
//
//        while (strIt != tmpStr.end() && patIt != tmpPattern.end())
//        {
//            if (*patIt == '*')
//            {
//                lastWildCardIt = patIt;
//                // Skip over looking for next character
//                ++patIt;
//
//                if (patIt == tmpPattern.end())
//                {
//                    // Skip right to the end since * matches the entire rest of the std::string
//                    strIt = tmpStr.end();
//                }
//                else
//                {
//                    // scan until we find next pattern character
//                    while (strIt != tmpStr.end() && *strIt != *patIt)
//                    {
//                        ++strIt;
//                    }
//                }
//            }
//            else
//            {
//                if (*patIt != *strIt)
//                {
//                    if (lastWildCardIt != tmpPattern.end())
//                    {
//                        // The last wildcard can match this incorrect sequence
//                        // rewind pattern to wildcard and keep searching
//                        patIt = lastWildCardIt;
//                        lastWildCardIt = tmpPattern.end();
//                    }
//                    else
//                    {
//                        // no wildwards left
//                        return false;
//                    }
//                }
//                else
//                {
//                    ++patIt;
//                    ++strIt;
//                }
//            }
//        }
//
//        // If we reached the end of both the pattern and the std::string, we succeeded
//        if (patIt == tmpPattern.end() && strIt == tmpStr.end())
//        {
//            return true;
//        }
//        else
//        {
//            return false;
//        }
//    }
//
//    static bool Match(const std::wstring& str, const std::wstring& pattern, bool case_sensitive = true)
//    {
//        std::wstring tmpStr = str;
//        std::wstring tmpPattern = pattern;
//
//        if (!case_sensitive)
//        {
//            AFStringUtils::ToLower(tmpStr);
//            AFStringUtils::ToLower(tmpPattern);
//        }
//
//        std::wstring::const_iterator strIt = tmpStr.begin();
//        std::wstring::const_iterator patIt = tmpPattern.begin();
//        std::wstring::const_iterator lastWildCardIt = tmpPattern.end();
//
//        while (strIt != tmpStr.end() && patIt != tmpPattern.end())
//        {
//            if (*patIt == L'*')
//            {
//                lastWildCardIt = patIt;
//                // Skip over looking for next character
//                ++patIt;
//
//                if (patIt == tmpPattern.end())
//                {
//                    // Skip right to the end since * matches the entire rest of the std::string
//                    strIt = tmpStr.end();
//                }
//                else
//                {
//                    // scan until we find next pattern character
//                    while (strIt != tmpStr.end() && *strIt != *patIt)
//                    {
//                        ++strIt;
//                    }
//                }
//            }
//            else
//            {
//                if (*patIt != *strIt)
//                {
//                    if (lastWildCardIt != tmpPattern.end())
//                    {
//                        // The last wildcard can match this incorrect sequence
//                        // rewind pattern to wildcard and keep searching
//                        patIt = lastWildCardIt;
//                        lastWildCardIt = tmpPattern.end();
//                    }
//                    else
//                    {
//                        // no wildwards left
//                        return false;
//                    }
//                }
//                else
//                {
//                    ++patIt;
//                    ++strIt;
//                }
//            }
//        }
//
//        // If we reached the end of both the pattern and the std::string, we succeeded
//        if (patIt == tmpPattern.end() && strIt == tmpStr.end())
//        {
//            return true;
//        }
//        else
//        {
//            return false;
//        }
//    }
//
//    // @brief Reports whether a char or a substr is within mother
//    // @param mother, the mother std::string to test
//    // @param pattern, the pattern std::string or char to find
//    // @param[in] case_sensitive true case sensitive, false ignore the case
//    // @return bool, return true if the occurrence of pattern within the motherStr or false
//    static bool Contains(const std::string& mother, char pattern, bool case_sensitive = true)
//    {
//        return stringUtil_contains(mother, pattern, case_sensitive);
//    }
//
//    static bool Contains(const std::wstring& mother, wchar_t pattern, bool case_sensitive = true)
//    {
//        return stringUtil_contains(mother, pattern, case_sensitive);
//    }
//
//    static bool Contains(const std::string& mother, const std::string& pattern, bool case_sensitive = true)
//    {
//        return stringUtil_contains(mother, pattern, case_sensitive);
//    }
//
//    static bool Contains(const std::wstring& mother, const std::wstring& pattern, bool case_sensitive = true)
//    {
//        return stringUtil_contains(mother, pattern, case_sensitive);
//    }
//
//    // query whether parameter std::string is a float number std::string or not.
//    static bool IsFloatNumber(std::string& s)
//    {
//        if (s.find('.') != std::string::npos || s.find('e') != std::string::npos || s.find('E') != std::string::npos)
//        {
//            return true;
//        }
//
//        return false;
//    }
//
//    static bool IsDigit(const std::string& str)
//    {
//        if (str.empty())
//        {
//            return false;
//        }
//
//        std::string::const_iterator start = str.begin();
//        if (*start == '-')
//        {
//            ++start;
//        }
//
//        return (std::all_of(start, str.end(), [](unsigned char c) { return isdigit(c); }));
//    }
//
//    // URL encode & decode
//    static bool URLEncode(const char* url, size_t url_len, char* edcoded_url, size_t& edcoded_url_len)
//    {
//        static unsigned char hexchars[] = "0123456789ABCDEF";
//
//        unsigned char c;
//        unsigned char *to, *start, *to_end;
//        unsigned char const *from, *end;
//
//        start = to = (unsigned char*)(edcoded_url);
//        to_end = to + edcoded_url_len;
//
//        from = (unsigned char const*)url;
//        end = from + url_len;
//
//        while (from < end)
//        {
//            c = *from++;
//
//            if (c == ' ')
//            {
//                if (to < to_end)
//                {
//                    *to++ = '+';
//                }
//                else
//                {
//                    return false;
//                }
//            }
//            else if ((c < '0' && c != '-' && c != '.') || (c < 'A' && c > '9') || (c > 'Z' && c < 'a' && c != '_') ||
//                     (c > 'z'))
//            {
//                if (to + 2 < to_end)
//                {
//                    to[0] = '%';
//                    to[1] = hexchars[c >> 4];
//                    to[2] = hexchars[c & 15];
//                    to += 3;
//                }
//                else
//                {
//                    return false;
//                }
//            }
//            else
//            {
//                if (to < to_end)
//                {
//                    *to++ = c;
//                }
//                else
//                {
//                    return false;
//                }
//            }
//        }
//        *to = 0;
//        edcoded_url_len = (to - start);
//        return true;
//    }
//
//    static std::string URLEncode(const std::string& str)
//    {
//        std::string out;
//        URLEncode(str, out);
//        return out;
//    }
//
//    static void URLEncode(const std::string& str, std::string& out)
//    {
//        URLEncode(str.data(), str.size(), out);
//    }
//
//    static void URLEncode(const char* url, size_t url_len, std::string& out)
//    {
//        size_t encoded_url_len = url_len * 3;
//        out.resize(encoded_url_len);
//        URLEncode(url, url_len, &out[0], encoded_url_len);
//        out.resize(encoded_url_len);
//    }
//
//    static std::string URLDecode(const std::string& str)
//    {
//        std::string out;
//        URLDecode(str, out);
//        return out;
//    }
//
//    static void URLDecode(const std::string& str, std::string& out)
//    {
//        URLDecode(str.data(), str.size(), out);
//    }
//
//    static void URLDecode(const char* encoded_url, size_t encoded_url_len, std::string& out)
//    {
//        out.resize(encoded_url_len);
//        size_t decoded_url_len = encoded_url_len;
//        URLDecode(encoded_url, encoded_url_len, &out[0], decoded_url_len);
//        out.resize(decoded_url_len);
//    }
//
//    static void URLDecode(const char* encoded_url, size_t encoded_url_len, char* decoded_url, size_t& decoded_url_len)
//    {
//        char* dest = decoded_url;
//        const char* data = encoded_url;
//        int len = (int)encoded_url_len;
//        while (len--)
//        {
//            if (*data == '+')
//            {
//                *dest = ' ';
//            }
//            else if (*data == '%' && len >= 2 && isxdigit((int)*(data + 1)) && isxdigit((int)*(data + 2)))
//            {
//                *dest = (char)php_htoi(data + 1);
//                data += 2;
//                len -= 2;
//            }
//            else
//            {
//                *dest = *data;
//            }
//            data++;
//            dest++;
//        }
//
//        decoded_url_len = dest - decoded_url;
//    }
//
//    static void URLDecode(std::string& str)
//    {
//        URLDecode(str, str);
//    }
//};
//
//} // namespace ark
