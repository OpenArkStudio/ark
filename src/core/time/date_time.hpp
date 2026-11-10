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

class timespan {
public:
    using time_diff = int64_t;

    static const time_diff millisecond = 1L;
    static const time_diff second_ms = 1000 * millisecond;
    static const time_diff minute_ms = 60 * second_ms;
    static const time_diff hour_ms = 60 * minute_ms;
    static const time_diff day_ms = 24 * hour_ms;
    static const time_diff week_ms = 7 * day_ms;
    const int month_day[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    timespan()
        : span_(0) {
    }

    explicit timespan(time_diff milliseconds)
        : span_(milliseconds) {
    }

    timespan(int seconds, int milliseconds)
        : span_(time_diff(seconds) * second_ms + milliseconds) {
    }

    timespan(int days, int hours, int seconds, int milliseconds)
        : span_(time_diff(milliseconds) + time_diff(seconds) * second_ms + time_diff(hours) * hour_ms +
                time_diff(days) * day_ms) {
    }

    timespan(const timespan& timespan)
        : span_(timespan.span_) {
    }

    ~timespan() {
    }

    timespan& operator=(const timespan& timespan) {
        if (this != &timespan) {
            this->span_ = timespan.span_;
        }
        return *this;
    }

    timespan& operator=(time_diff microseconds) {
        span_ = microseconds;
        return *this;
    }

    timespan& assign(int days, int hours, int minutes, int seconds, int milliseconds = 0) {
        span_ = time_diff(milliseconds) + time_diff(seconds) * second_ms + time_diff(hours) * hour_ms +
                time_diff(days) * day_ms;
        return *this;
    }

    timespan& assign(int seconds, int milliseconds = 0) {
        span_ = time_diff(milliseconds) + time_diff(seconds) * second_ms;
        return *this;
    }

    void swap(timespan& timespan) noexcept {
        std::swap(span_, timespan.span_);
    }

    inline bool operator==(const timespan& ts) const {
        return span_ == ts.span_;
    }

    inline bool operator!=(const timespan& ts) const {
        return span_ != ts.span_;
    }

    inline bool operator>(const timespan& ts) const {
        return span_ > ts.span_;
    }

    inline bool operator>=(const timespan& ts) const {
        return span_ >= ts.span_;
    }

    inline bool operator<(const timespan& ts) const {
        return span_ < ts.span_;
    }

    inline bool operator<=(const timespan& ts) const {
        return span_ <= ts.span_;
    }

    inline bool operator==(time_diff milliseconds) const {
        return span_ == milliseconds;
    }

    inline bool operator!=(time_diff milliseconds) const {
        return span_ != milliseconds;
    }

    inline bool operator>(time_diff milliseconds) const {
        return span_ > milliseconds;
    }

    inline bool operator>=(time_diff milliseconds) const {
        return span_ >= milliseconds;
    }

    inline bool operator<(time_diff milliseconds) const {
        return span_ < milliseconds;
    }

    inline bool operator<=(time_diff milliseconds) const {
        return span_ <= milliseconds;
    }

    inline timespan operator+(const timespan& d) const {
        return timespan(span_ + d.span_);
    }

    inline timespan operator-(const timespan& d) const {
        return timespan(span_ - d.span_);
    }

    inline timespan& operator+=(const timespan& d) {
        span_ += d.span_;
        return *this;
    }

    inline timespan& operator-=(const timespan& d) {
        span_ -= d.span_;
        return *this;
    }

    inline timespan operator+(time_diff milliseconds) const {
        return timespan(span_ + milliseconds);
    }

    inline timespan operator-(time_diff milliseconds) const {
        return timespan(span_ - milliseconds);
    }

    inline timespan& operator+=(time_diff milliseconds) {
        span_ += milliseconds;
        return *this;
    }

    inline timespan& operator-=(time_diff milliseconds) {
        span_ -= milliseconds;
        return *this;
    }

    inline int days() const {
        return int(span_ / day_ms);
    }

    inline int hours() const {
        return int((span_ / hour_ms) % 24);
    }

    inline int total_hours() const {
        return int((span_ / hour_ms));
    }

    inline int minutes() const {
        return int((span_ / minute_ms) % 60);
    }

    inline int total_minutes() const {
        return int(span_ / minute_ms);
    }

    inline int seconds() const {
        return int((span_ / second_ms) % 60);
    }

    inline int total_seconds() const {
        return int(span_ / second_ms);
    }

    inline int milliseconds() const {
        return int(span_ % 1000);
    }

    inline time_diff total_milliseconds() const {
        return span_;
    }

private:
    // milliseconds
    time_diff span_;
};

////////////////////////////////////////////////////////////////////////

class date_time {
public:
    enum months {
        january = 1,
        february,
        march,
        april,
        may,
        june,
        july,
        august,
        september,
        october,
        november,
        december
    };

    enum days_of_week {
        sunday = 0,
        monday,
        tuesday,
        wednesday,
        thursday,
        friday,
        saturday
    };

    // Monotonic UTC time value in microsecond resolution,
    // with base time midnight, January 1, 1970.
    using time_val = int64_t;
    // Difference between two TimeVal values in milliseconds.
    using time_diff = int64_t;

    static const time_val timeval_min = (std::numeric_limits<int64_t>::min)();
    static const time_val timeval_max = (std::numeric_limits<int64_t>::max)();

    // Creates a timestamp with the current time.
    date_time() {
        update();
    }

    // Creates a timestamp from the given time value
    //(milliseconds since midnight, January 1, 1970).
    date_time(time_val tv)
        : ts_(tv) {
    }

    date_time(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int tzd = 8) {
        struct tm tmp_tm;
        tmp_tm.tm_year = year - 1900;
        tmp_tm.tm_mon = month - 1;
        tmp_tm.tm_mday = day;
        tmp_tm.tm_hour = hour;
        tmp_tm.tm_min = minute;
        tmp_tm.tm_sec = second;
        tmp_tm.tm_isdst = -1;
        time_t time = std::mktime(&tmp_tm);
        ts_ = time_val(time * resolution()) + tzd * timespan::hour_ms;
    }

    // Copy constructor
    date_time(const date_time& other) {
        ts_ = other.ts_;
    }

    // Destroys the timestamp
    ~date_time() = default;

    date_time& operator=(const date_time& other) {
        if (this != &other) {
            ts_ = other.ts_;
        }

        return *this;
    }

    date_time& operator=(time_val& tv) {
        ts_ = tv;
        return *this;
    }

    void update() {
        using namespace std::chrono;
        ts_ = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    /// Swaps the Timestamp with another one.
    void swap(date_time& timestamp) noexcept {
        std::swap(ts_, timestamp.ts_);
    }

    bool operator==(const date_time& ts) const {
        return ts_ == ts.ts_;
    }

    bool operator!=(const date_time& ts) const {
        return ts_ != ts.ts_;
    }

    bool operator>(const date_time& ts) const {
        return ts_ > ts.ts_;
    }

    bool operator>=(const date_time& ts) const {
        return ts_ >= ts.ts_;
    }

    bool operator<(const date_time& ts) const {
        return ts_ < ts.ts_;
    }

    bool operator<=(const date_time& ts) const {
        return ts_ <= ts.ts_;
    }

    date_time operator+(time_diff d) const {
        return date_time(ts_ + d);
    }

    date_time operator-(time_diff d) const {
        return date_time(ts_ - d);
    }

    date_time& operator+=(time_diff d) {
        ts_ += d;
        return *this;
    }

    date_time& operator-=(time_diff d) {
        ts_ -= d;
        return *this;
    }

    date_time operator+(const timespan& span) const {
        return *this + span.total_milliseconds();
    }

    date_time operator-(const timespan& span) const {
        return *this - span.total_milliseconds();
    }

    time_diff operator-(const date_time& ts) const {
        return ts_ - ts.ts_;
    }

    date_time& operator+=(const timespan& span) {
        return *this += span.total_milliseconds();
    }

    date_time& operator-=(const timespan& span) {
        return *this -= span.total_milliseconds();
    }

    // Returns the timestamp expressed in time_t.
    // ime_t base time is midnight, January 1, 1970.
    // Resolution is one second.
    std::time_t get_time() const {
        return std::time_t(ts_ / resolution());
    }

    // Returns the timestamp expressed in milliseconds
    // since the Unix epoch, midnight, January 1, 1970.
    time_val get_milliseconds() {
        return ts_;
    }

    // Returns the time elapsed since the time denoted by
    // the timestamp. Equivalent to Timestamp() - *this.
    time_diff get_elapsed() const {
        date_time now;
        return now - *this;
    }

    // Returns true diff the given interval has passed
    // since the time denoted by the timestamp.
    bool is_elapsed(time_diff interval) const {
        date_time now;
        time_diff diff = now - *this;
        return diff >= interval;
    }

    // Returns the raw time value.
    // Same as epochMilliseconds().
    time_val raw() const {
        return ts_;
    }

    // Returns utc tm data
    std::tm get_utc_time() const {
        std::tm tm;
        time_t time = get_time();
#ifdef ARK_PLATFORM_WIN
        gmtime_s(&tm, &time);
#else
        gmtime_r(&time, &tm);
#endif
        return tm;
    }

    // Return local tm data
    std::tm get_local_time() const {
        time_t time = get_time();
        std::tm tm;
#ifdef ARK_PLATFORM_WIN
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        return tm;
    }

    // Returns the year.
    int get_year() const {
        std::tm tm = get_utc_time();
        return tm.tm_year + 1900;
    }

    // Returns the month (1 to 12).
    int get_month() const {
        std::tm tm = get_utc_time();
        return tm.tm_mon + 1;
    }

    /// Returns the week number within the year.
    /// FirstDayOfWeek should be either SUNDAY (0) or MONDAY (1).
    /// The returned week number will be from 0 to 53. Week number 1 is the week
    /// containing January 4. This is in accordance to ISO 8601.
    ///
    /// The following example assumes that firstDayOfWeek is MONDAY. For 2005, which started
    /// on a Saturday, week 1 will be the week starting on Monday, January 3.
    /// January 1 and 2 will fall within week 0 (or the last week of the previous year).
    ///
    /// For 2007, which starts on a Monday, week 1 will be the week starting on Monday, January 1.
    /// There will be no week 0 in 2007.
    int get_week_of_year(int first_day_of_week = monday) const {
        ARK_ASSERT_NO_EFFECT(first_day_of_week >= sunday && first_day_of_week <= saturday);

        // find the first firstDayOfWeek.
        int base_day = 1;

        while (date_time(get_year(), 1, base_day).get_day_of_week() != first_day_of_week) {
            ++base_day;
        }

        //baseDay is the dayofYear of first firstDayOfWeek and from 1.
        //day as the dayOfYear of currentDay should also from 1.
        //otherwise (day-baseDay) fail to represent the gap_days between them.
        //GetDayOfYear start from 0
        //so day should plus one
        int day = get_day_of_year();
        day++;
        int offs = base_day <= 4 ? 0 : 1; //weekofYear of baseDay
        if (day < base_day) {
            return offs;
        } else {
            return (day - base_day) / 7 + 1 + offs;
        }
    }

    // Returns the day within the month (1 to 31).
    int get_day(bool is_monday_first_day = true) const {
        std::tm tm = get_utc_time();
        return tm.tm_mday;
    }

    // Returns the weekday (0 to 6, where
    // 0 = Sunday, 1 = Monday, ..., 6 = Saturday).
    int get_day_of_week(int first_day_of_week = monday) const {
        std::tm tm = get_utc_time();
        if (first_day_of_week == monday) {
            return ((tm.tm_wday == 0) ? 7 : tm.tm_wday);
        } else {
            return tm.tm_wday;
        }
    }

    // Returns the number of the day in the year.
    // January 1 is 1, February 1 is 32, etc.
    int get_day_of_year() const {
        std::tm tm = get_utc_time();
        return tm.tm_yday;
    }

    // Returns the hour (0 to 23).
    int get_hour() const {
        std::tm tm = get_utc_time();
        return tm.tm_hour;
    }

    // Returns the hour (0 to 12).
    int gethour_ampm() const {
        int hour = get_hour();

        if (hour < 1) {
            return 12;
        } else if (hour > 12) {
            return hour - 12;
        } else {
            return hour;
        }
    }

    /// Returns true if hour < 12;
    bool is_am() const {
        return get_hour() < 12;
    }

    /// Returns true if hour >= 12.
    bool is_pm() const {
        return get_hour() >= 12;
    }

    /// Returns the minute (0 to 59).
    int get_minute() const {
        std::tm tm = get_utc_time();
        return tm.tm_min;
    }

    /// Returns the second (0 to 59).
    int get_second() const {
        std::tm tm = get_utc_time();
        return tm.tm_sec;
    }

    bool is_leap_year() const {
        int year = get_year();
        return is_leap_year(year);
    }

    bool same_minute(time_val time) {
        date_time x_time(time);
        return (x_time.get_minute() == get_minute() && x_time.get_hour() == get_hour() &&
                x_time.get_day() == get_day() && x_time.get_month() == get_month() && x_time.get_year() == get_year());
    }

    bool same_hour(time_val time) {
        date_time x_time(time);
        return (x_time.get_hour() == get_hour() && x_time.get_day() == get_day() && x_time.get_month() == get_month() &&
                x_time.get_year() == get_year());
    }

    bool same_day(time_val time) {
        date_time x_time(time);
        return (x_time.get_day() == get_day() && x_time.get_month() == get_month() && x_time.get_year() == get_year());
    }

    bool same_month(time_val time) {
        date_time x_time(time);
        return (x_time.get_month() == get_month() && x_time.get_year() == get_year());
    }

    bool same_year(time_val time) {
        date_time x_time(time);
        return (x_time.get_year() == get_year());
    }

    bool same_week(time_val time) {
        if (!same_year(time)) {
            return false;
        }

        if (same_day(time)) {
            return true;
        }

        date_time x_time(time);
        return (get_week_of_year() == x_time.get_week_of_year());
    }

    std::string to_string() {
        std::tm tm = get_local_time(); // local time
        static char time_buff[32] = {0};
        std::strftime(time_buff, sizeof(time_buff), "%Y/%m/%d %H:%M:%S", &tm);
        return std::string(time_buff);
    }

    std::string to_iso8601_string() {
        std::tm tm = get_utc_time(); // UTC time
        static char time_buff[64] = {0};
        std::strftime(time_buff, sizeof(time_buff), "%FT%TZ", &tm);
        return std::string(time_buff);
    }

    static bool is_leap_year(int year) {
        return (year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0);
    }

    // Returns the number of days in the given month
    // and year. Month is from 1 to 12.
    static int get_days_of_month(int year, int month) {
        ARK_ASSERT_NO_EFFECT(month >= 1 && month <= 12);

        if (month < 1 || month > 12) {
            return -1;
        }

        static int days_of_month_table[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

        if (month == 2 && is_leap_year(year)) {
            return 29;
        } else {
            return days_of_month_table[month];
        }
    }

    // Checks if the given date and time is valid
    // all arguments are within a proper range).
    //
    // Returns true if all arguments are valid, false otherwise.
    static bool is_valid(
        int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int millisecond = 0) {
        return (year >= 0 && year <= 9999) && (month >= 1 && month <= 12) &&
               (day >= 1 && day <= get_days_of_month(year, month)) && (hour >= 0 && hour <= 23) &&
               (minute >= 0 && minute <= 59) && (second >= 0 && second <= 60) &&
               (millisecond >= 0 && millisecond <= 999);
    }

    // Creates a timestamp static
    // Returns milliseconds
    static time_val get_now_time() {
        date_time now;
        return now.raw();
    }

    // Creates a timestamp static
    // Returns seconds
    static int get_timestamp() {
        date_time now;
        return (int)now.get_time();
    }

    static time_diff resolution() {
        return 1000; // 1s = 1000 milliseconds
    }

private:
    // milliseconds
    time_val ts_;
};

inline void swap(date_time& d1, date_time& d2) noexcept {
    d1.swap(d2);
}

} // namespace ark
