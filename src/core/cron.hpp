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

#include "ark/platform.hpp"

// this file is based on https://github.com/Bosma/Scheduler.
// under the MIT license.

namespace ark {

class bad_cron_expression : public std::exception
{
public:
    explicit bad_cron_expression(const std::string&& msg)
        : msg_(std::move(msg)) {
    }

    const char* what() const noexcept override {
        return (msg_.c_str());
    }

private:
    std::string msg_;
};

namespace detail {
inline std::tm scheduler_local_time(std::time_t time) {
    std::tm result{};
#ifdef ARK_PLATFORM_WIN
    const bool converted = localtime_s(&result, &time) == 0;
#else
    const bool converted = localtime_r(&time, &result) != nullptr;
#endif
    if (!converted)
    {
        throw std::runtime_error("Cannot convert scheduler local time");
    }
    return result;
}
} // namespace detail

inline void add(std::tm& tm, std::chrono::system_clock::duration time) {
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    auto tp_adjusted = tp + time;
    auto tm_adjusted = std::chrono::system_clock::to_time_t(tp_adjusted);
    tm = detail::scheduler_local_time(tm_adjusted);
}

class cron
{
public:
    using clock = std::chrono::system_clock;

    explicit cron(const std::string& expression) {
        std::istringstream iss(expression);
        std::vector<std::string> tokens{std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{}};

        if (tokens.size() != 5)
        {
            throw bad_cron_expression("malformed cron string (must be 5 fields): " + expression);
        }

        verify_and_set(tokens[0], expression, minute, 0, 59);
        verify_and_set(tokens[1], expression, hour, 0, 23);
        verify_and_set(tokens[2], expression, day, 1, 31);
        verify_and_set(tokens[3], expression, month, 1, 12, true);
        verify_and_set(tokens[4], expression, day_of_week, 0, 6);
    }

    // http://stackoverflow.com/a/322058/1284550
    clock::time_point cron_to_next(const clock::time_point from = clock::now()) const {
        // get current time as a tm object
        auto now = clock::to_time_t(from);
        std::tm next = detail::scheduler_local_time(now);
        // it will always at least run the next minute
        next.tm_sec = 0;
        add(next, std::chrono::minutes(1));
        while (true)
        {
            if (month != -1 && next.tm_mon != month)
            {
                // add a month
                // if this will bring us over a year, increment the year instead and reset the month
                if (next.tm_mon + 1 > 11)
                {
                    next.tm_mon = 0;
                    next.tm_year++;
                }
                else
                {
                    next.tm_mon++;
                }

                next.tm_mday = 1;
                next.tm_hour = 0;
                next.tm_min = 0;
                continue;
            }

            if (day != -1 && next.tm_mday != day)
            {
                add(next, std::chrono::hours(24));
                next.tm_hour = 0;
                next.tm_min = 0;
                continue;
            }

            if (day_of_week != -1 && next.tm_wday != day_of_week)
            {
                add(next, std::chrono::hours(24));
                next.tm_hour = 0;
                next.tm_min = 0;
                continue;
            }

            if (hour != -1 && next.tm_hour != hour)
            {
                add(next, std::chrono::hours(1));
                next.tm_min = 0;
                continue;
            }

            if (minute != -1 && next.tm_min != minute)
            {
                add(next, std::chrono::minutes(1));
                continue;
            }

            break;
        }

        // telling mktime to figure out dst
        next.tm_isdst = -1;
        return clock::from_time_t(std::mktime(&next));
    }

    int minute{0};
    int hour{0};
    int day{0};
    int month{0};
    int day_of_week{0};

private:
    inline void verify_and_set(const std::string& token, const std::string& expression, int& field,
        const int lower_bound, const int upper_bound, const bool adjust = false) {
        if (token == "*")
        {
            field = -1;
        }
        else
        {
            try
            { field = std::stoi(token); }
            catch (const std::invalid_argument&)
            { throw bad_cron_expression("malformed cron string (`" + token + "` not an integer or *): " + expression); }
            catch (const std::out_of_range&)
            {
                throw bad_cron_expression(
                    "malformed cron string (`" + token + "` not convertable to int): " + expression);
            }

            if (field < lower_bound || field > upper_bound)
            {
                std::ostringstream oss;
                oss << "malformed cron string ('" << token << "' must be <= " << upper_bound
                    << " and >= " << lower_bound << "): " << expression;
                throw bad_cron_expression(oss.str());
            }

            if (adjust)
            {
                field--;
            }
        }
    }
};
} // namespace ark
