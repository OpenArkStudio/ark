// C++ 11实现的lexical_cast，为了剥离boost
//代码来自: https://github.com/qicosmos/cosmos

#ifndef CPP_11_LEXICAL_CAST_HPP
#define CPP_11_LEXICAL_CAST_HPP

#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace std;

namespace converter {

static const char* strue = "true";
static const char* sfalse = "false";

template<typename to_type, typename from_type>
struct value_converter {};

// to numeric
template<typename from_type>
struct value_converter<uint8_t, from_type> {
    static uint8_t convert(const string& from) {
        return std::atoi(from.c_str());
    }

    static uint8_t convert(const char* from) {
        return std::atoi(from);
    }
};

template<typename from_type>
struct value_converter<int16_t, from_type> {
    static int16_t convert(const string& from) {
        return std::atoi(from.c_str());
    }

    static int16_t convert(const char* from) {
        return std::atoi(from);
    }
};

template<typename from_type>
struct value_converter<uint16_t, from_type> {
    static uint16_t convert(const string& from) {
        return std::atoi(from.c_str());
    }

    static uint16_t convert(const char* from) {
        return std::atoi(from);
    }
};

template<typename from_type>
struct value_converter<int32_t, from_type> {
    static int32_t convert(const string& from) {
        return std::atoi(from.c_str());
    }

    static int32_t convert(const char* from) {
        return std::atoi(from);
    }
};

template<typename from_type>
struct value_converter<uint32_t, from_type> {
    static uint32_t convert(const string& from) {
        return static_cast<uint32_t>(std::strtoul(from.c_str(), nullptr, 10));
    }

    static uint32_t convert(const char* from) {
        return static_cast<uint32_t>(std::strtoul(from, nullptr, 10));
    }
};

template<typename from_type>
struct value_converter<long, from_type> {
    static long convert(const string& from) {
        return std::atol(from.c_str());
    }

    static long convert(const char* from) {
        return std::atol(from);
    }
};

template<typename from_type>
struct value_converter<long long, from_type> {
    static long long convert(const string& from) {
        return std::atoll(from.c_str());
    }

    static long long convert(const char* from) {
        return std::atoll(from);
    }
};

template<typename from_type>
struct value_converter<uint64_t, from_type> {
    static uint64_t convert(const string& from) {
        return static_cast<uint64_t>(std::strtoull(from.c_str(), nullptr, 10));
    }

    static uint64_t convert(const char* from) {
        return static_cast<uint64_t>(std::strtoull(from, nullptr, 10));
    }
};

template<typename from_type>
struct value_converter<double, from_type> {
    static double convert(const string& from) {
        return std::atof(from.c_str());
    }

    static double convert(const char* from) {
        return std::atof(from);
    }
};

template<typename from_type>
struct value_converter<float, from_type> {
    static float convert(const string& from) {
        return (float)std::atof(from.c_str());
    }

    static float convert(const char* from) {
        return (float)std::atof(from);
    }
};

// to bool
template<std::integral from_type>
struct value_converter<bool, from_type> {
    static bool convert(from_type from) {
        return !!from;
    }
};

static bool checkbool(const char* from, const size_t len, const char* s) {
    for (size_t i = 0; i < len; i++) {
        if (from[i] != s[i]) {
            return false;
        }
    }

    return true;
}

static bool convert(const char* from) {
    size_t len = strlen(from);

    bool r = true;
    if (len == 4) {
        //"true"
        r = checkbool(from, len, strue);

        if (r) {
            return true;
        }
    } else if (len == 5) {
        //"false"
        r = checkbool(from, len, sfalse);

        if (r) {
            return false;
        }
    } else {
        // 数字转为bool
        int value = value_converter<int, const char*>::convert(from);
        return (value > 0);
    }

    throw std::invalid_argument("argument is invalid");
}

template<>
struct value_converter<bool, string> {
    static bool convert(const string& from) {
        return converter::convert(from.c_str());
    }
};

template<>
struct value_converter<bool, const char*> {
    static bool convert(const char* from) {
        return converter::convert(from);
    }
};

template<>
struct value_converter<bool, char*> {
    static bool convert(char* from) {
        return converter::convert(from);
    }
};

template<unsigned extent>
struct value_converter<bool, const char[extent]> {
    static bool convert(const char (&from)[extent]) {
        return converter::convert(from);
    }
};

template<unsigned extent>
struct value_converter<bool, char[extent]> {
    static bool convert(const char (&from)[extent]) {
        return converter::convert(from);
    }
};

// to string
template<typename from_type>
struct value_converter<string, from_type> {
    static string convert(const from_type& from) {
        return std::to_string(from);
    }
};
} // namespace converter

template<typename to_type, typename from_type>
to_type lexical_cast(const from_type& from) {
    if constexpr (std::same_as<to_type, from_type>) {
        return from;
    } else {
        return converter::value_converter<to_type, from_type>::convert(from);
    }
}

template<typename value_type>
bool value_from_string(const std::string& input, value_type& value) {
    try {
        value = lexical_cast<value_type>(input);
        return true;
    } catch (...) {
        return false;
    }

    return false;
}

template<typename value_type>
bool value_to_string(const value_type& value, std::string& output) {
    try {
        output = lexical_cast<std::string>(value);
        return true;
    } catch (...) {
        return false;
    }

    return false;
}
#endif //! CPP_11_LEXICAL_CAST_HPP
