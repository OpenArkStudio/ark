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

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "ark/time/date_time.hpp"

TEST_CASE("GetMilliseconds() should return now_UTC_time", "[GetMilliseconds()]") {
    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.get_milliseconds() ==
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
}

TEST_CASE("default constructor should return AFDateTime instance with now_UTC_time", " AFDateTime()") {
    ark::date_time date_time_instance{};
    REQUIRE(date_time_instance.get_milliseconds() ==
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
}

TEST_CASE("using TimeVal constructor should return instance with the same TimeVal", " AFDateTime(TimeVal tv)") {
    auto time_val =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    ark::date_time date_time_instance{time_val};
    REQUIRE(date_time_instance.get_milliseconds() == time_val);
}

TEST_CASE("using year_month_day_hour_minute_second_tzd constructor should return instance with same value",
    "AFDateTime(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int tzd )") {
    ark::date_time date_time_provider{};
    int year = date_time_provider.get_year();
    int month = date_time_provider.get_month();
    int day = date_time_provider.get_day();
    int hour = date_time_provider.get_hour();
    int minute = date_time_provider.get_minute();
    int second = date_time_provider.get_second();

    ark::date_time date_time_instance(year, month, day, hour, minute, second);
    REQUIRE(date_time_instance.get_time() == date_time_provider.get_time()); //accurate to seconds
}

TEST_CASE("same AFdateTime instance, == should return true", "bool operator==(const AFDateTime& ts) const") {
    ark::date_time date_time_instance(5);
    ark::date_time date_time_control_group(5);
    REQUIRE(date_time_instance == date_time_control_group);
}

TEST_CASE("copy constructor should return instance same as copied instance", "AFDateTime(const AFDateTime& other)") {
    ark::date_time date_time_provider;
    ark::date_time date_time_instance{date_time_provider};
    REQUIRE(date_time_provider == date_time_instance);
}

TEST_CASE("= constructor with AFDateTime instance should return another instance same as assigned instance",
    "AFDateTime& operator=(const AFDateTime& other)") {
    ark::date_time date_time_provider;
    ark::date_time date_time_instance = date_time_provider;
    REQUIRE(date_time_instance == date_time_provider);
}

TEST_CASE("= constructor with timeval should return instance with same timeval", "AFDateTime& operator=(TimeVal& tv)") {
    ark::date_time::time_val tv_provider =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    ark::date_time date_time_instance = tv_provider;
    REQUIRE(date_time_instance.get_milliseconds() == tv_provider);
}

TEST_CASE("two AFdateTime instances should be swapped", "void swap(AFDateTime& timestamp)") {
    ark::date_time date_time_instance;
    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);
    REQUIRE(date_time_instance < date_time_control_group);
    date_time_instance.swap(date_time_control_group);
    REQUIRE(date_time_instance > date_time_control_group);
}

TEST_CASE("two different AFdateTime should return false", "bool operator!=(const AFDateTime& ts) const") {
    ark::date_time date_time_instance;
    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);
    REQUIRE(date_time_instance != date_time_control_group);
}

TEST_CASE(
    "instance with larger timeval > instance with smaller timeval", "bool operator>(const AFDateTime& ts) const") {
    ark::date_time date_time_instance;
    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);
    REQUIRE(date_time_control_group > date_time_instance);
}

TEST_CASE("instance with larger or same timeval >= instance with smaller or same timeval",
    "bool operator>=(const AFDateTime& ts) const") {
    ark::date_time date_time_instance;
    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);
    REQUIRE(date_time_control_group >= date_time_instance);
    REQUIRE(date_time_control_group >= date_time_control_group);
}

TEST_CASE(
    "instance with smaller timeval < instance with larger timeval", "bool operator<(const AFDateTime& ts) const") {
    ark::date_time date_time_instance;
    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);
    REQUIRE(date_time_instance < date_time_control_group);
}

TEST_CASE("instance with smaller  or same timeval < instance with larger or same timeval",
    "bool operator<=(const AFDateTime& ts) const") {
    ark::date_time date_time_instance;
    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);
    REQUIRE(date_time_instance <= date_time_control_group);
    REQUIRE(date_time_instance <= date_time_instance);
}

TEST_CASE("old instance plus Tiemdiff should return plused timeval", "AFDateTime operator+(TimeDiff d) const") {
    ark::date_time date_time_instance;
    ark::date_time::time_val old_tv = date_time_instance.get_milliseconds();

    date_time_instance = date_time_instance + (ark::date_time::time_diff)2;
    REQUIRE(date_time_instance.get_milliseconds() == old_tv + (ark::date_time::time_diff)2);
}

TEST_CASE("old instance minus Timediff should return minused timeval", "AFDateTime operator-(TimeDiff d) const") {
    ark::date_time date_time_instance;
    ark::date_time::time_val old_tv = date_time_instance.get_milliseconds();

    date_time_instance = date_time_instance - (ark::date_time::time_diff)2;
    REQUIRE(date_time_instance.get_milliseconds() == old_tv - (ark::date_time::time_diff)2);
}

TEST_CASE("overload operator +=(Timediff)", "AFDateTime& operator+=(TimeDiff d)") {
    ark::date_time date_time_instance;
    ark::date_time::time_val old_tv = date_time_instance.get_milliseconds();

    date_time_instance += (ark::date_time::time_diff)2;
    REQUIRE(date_time_instance.get_milliseconds() == old_tv + (ark::date_time::time_diff)2);
}

TEST_CASE("overload operator -=(Timediff)", "AFDateTime& operator-=(TimeDiff d)") {
    ark::date_time date_time_instance;
    ark::date_time::time_val old_tv = date_time_instance.get_milliseconds();

    date_time_instance -= (ark::date_time::time_diff)2;
    REQUIRE(date_time_instance.get_milliseconds() == old_tv - (ark::date_time::time_diff)2);
}

TEST_CASE("overload operator +(AFTimespan)", "AFDateTime operator+(const AFTimespan& span) const") {
    ark::date_time date_time_instance;
    ark::date_time::time_val old_tv = date_time_instance.get_milliseconds();

    date_time_instance = date_time_instance + ark::timespan(2);
    REQUIRE(date_time_instance.get_milliseconds() == old_tv + (ark::date_time::time_diff)2);
}

TEST_CASE("overload operator -(AFTimespan)", " AFDateTime operator-(const AFTimespan& span) const") {
    ark::date_time date_time_instance;
    ark::date_time::time_val old_tv = date_time_instance.get_milliseconds();

    date_time_instance = date_time_instance - ark::timespan(2);
    REQUIRE(date_time_instance.get_milliseconds() == old_tv - (ark::date_time::time_diff)2);
}

TEST_CASE("overload operator -(AFdateTime)", " TimeDiff operator-(const AFDateTime& ts) const") {
    ark::date_time date_time_instance;
    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);
    REQUIRE(date_time_control_group - date_time_instance ==
            date_time_control_group.get_milliseconds() - date_time_instance.get_milliseconds());
}

TEST_CASE("overload operator +=(AFTimeSpan)", " AFDateTime& operator+=(const AFTimespan& span)") {
    ark::date_time date_time_instance;

    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);

    date_time_instance += ark::timespan(date_time_control_group - date_time_instance);
    REQUIRE(date_time_instance == date_time_control_group);
}

TEST_CASE("overload operator -=(AFTimeSpan)", " AFDateTime& operator-=(const AFTimespan& span)") {
    ark::date_time date_time_instance;

    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);

    date_time_control_group -= ark::timespan(date_time_control_group - date_time_instance);
    REQUIRE(date_time_instance == date_time_control_group);
}

TEST_CASE("should return std::time_t", "  std::time_t GetTime() const") {
    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.get_time() == std::time(0));
}

TEST_CASE("should return timediff between current time", "  TimeDiff GetElapsed() const") {
    ark::date_time date_time_instance;
    ark::date_time date_time_control_group(date_time_instance.get_milliseconds() + 5);
    REQUIRE(date_time_instance.get_elapsed() == ark::date_time::time_diff(ark::date_time() - date_time_instance));
}

TEST_CASE("return interval not equal with timediff between current time", " bool IsElapsed(TimeDiff interval) const") {
    ark::date_time date_time_instance;
    date_time_instance -= 20;
    REQUIRE(date_time_instance.is_elapsed(19) == true);
}

TEST_CASE("return time ago", " TimeVal Raw() const") {
    ark::date_time date_time_instance;
    date_time_instance -= 5;

    REQUIRE(date_time_instance.raw() == date_time_instance.get_milliseconds());
    REQUIRE(date_time_instance.raw() !=
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
}

TEST_CASE("return time in 0 time_zone", "std::tm GetUTCTime() const") {
    time_t control_group;
    time(&control_group);

    ark::date_time date_time_instance;
    std::tm current_time = date_time_instance.get_utc_time();
    REQUIRE(mktime(&current_time) == mktime(gmtime(&control_group)));
}

TEST_CASE("return time in system_time_zone", " std::tm GetLocalTime() const") {
    time_t control_group;
    time(&control_group);

    ark::date_time date_time_instance;
    std::tm current_time = date_time_instance.get_local_time();
    REQUIRE(mktime(&current_time) == mktime(localtime(&control_group)));
}

TEST_CASE("return year", "int GetYear() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);
    std::tm* p = localtime(&control_group);
    REQUIRE(date_time_instance.get_year() == p->tm_year + 1900);
}

TEST_CASE("return month", "int GetMonth() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);
    std::tm* p = localtime(&control_group);
    REQUIRE(date_time_instance.get_month() == p->tm_mon + 1);
}

TEST_CASE("return day_of_week", "int GetDayOfWeek(int firstDayOfWeek = MONDAY) const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);
    std::tm* p = localtime(&control_group);
    REQUIRE(date_time_instance.get_day_of_week() == p->tm_wday);
}

TEST_CASE("return day_of_year", "int GetDayOfYear() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);
    REQUIRE(date_time_instance.get_day_of_year() == localtime(&control_group)->tm_yday);
}

void test_week_of_year(int y, int m, int d) {
    ark::date_time date_time_instance(y, m, d);
    auto week = date_time_instance.get_week_of_year();
    auto day = date_time_instance.get_day_of_week();
    day = day > 0 ? day : 7;
    auto day_of_year = date_time_instance.get_day_of_year(); //Jan_1 is 0
    day_of_year++;
    auto _day = ark::date_time(date_time_instance.get_year(), 1, 4).get_day_of_week();
    _day = _day > 0 ? _day : 7;
    int day_reduce;
    day_reduce = (week - 1) * 7 + 4 + day - _day;
    REQUIRE(day_of_year == day_reduce);
}

TEST_CASE("return week_of_year", "int GetWeekOfYear(int firstDayOfWeek = MONDAY) const") {
    int days_of_month_table[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int y = 2017; y <= 2019; y++) {
        for (int i = 1; i <= 12; i++) {
            for (int j = 1; j <= days_of_month_table[i]; j++) {
                test_week_of_year(y, i, j);
            }
        }
    }
}

TEST_CASE("return day of month", "int GetDay(bool is_monday_first_day = true) const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);
    REQUIRE(date_time_instance.get_day() == localtime(&control_group)->tm_mday);
}

TEST_CASE("return hour of day", "int GetHour() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);

    auto tzd = 8;
    REQUIRE(date_time_instance.get_hour() == localtime(&control_group)->tm_hour - tzd);
}

TEST_CASE("return hour of day in am/pm format", "int GethourAMPM() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);
    std::tm* p = localtime(&control_group);

    int tzd = 8;
    p->tm_hour -= tzd;
    if (p->tm_hour < 1) {
        p->tm_hour = 12;
    } else if (p->tm_hour > 12) {
        p->tm_hour -= 12;
    }
    REQUIRE(date_time_instance.get_hour() == p->tm_hour);
}

TEST_CASE("return hour of day whether in am", "bool IsAM() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);
    std::tm* p = localtime(&control_group);

    int tzd = 8;
    p->tm_hour -= tzd;
    bool isam = false;
    if (p->tm_hour < 12) {
        isam = true;
    }
    REQUIRE(date_time_instance.is_am() == isam);
}

TEST_CASE("return hour of day whether in pm", "bool IsPM() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);
    std::tm* p = localtime(&control_group);

    int tzd = 8;
    p->tm_hour -= tzd;
    bool ispm = false;
    if (p->tm_hour >= 12) {
        ispm = true;
    }
    REQUIRE(date_time_instance.is_pm() == ispm);
}

TEST_CASE("return minute of hour", "int GetMinute() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);

    REQUIRE(date_time_instance.get_minute() == localtime(&control_group)->tm_min);
}

TEST_CASE("return second of minute", "int GetSecond() const") {
    ark::date_time date_time_instance;

    time_t control_group;
    time(&control_group);

    REQUIRE(date_time_instance.get_second() == localtime(&control_group)->tm_sec);
}

TEST_CASE("return whether LeapYear", "bool IsLeapYear() const") {
    ark::date_time date_time_instance;
    auto year = date_time_instance.get_year();
    REQUIRE(date_time_instance.is_leap_year() == ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0)));
}

TEST_CASE("return whether same minute", "bool SameMinute(TimeVal time)") {
    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.same_minute(date_time_instance.get_milliseconds()) == true);
}

TEST_CASE("return whether same hour", "bool SameHour(TimeVal time)") {
    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.same_hour(date_time_instance.get_milliseconds()) == true);
}

TEST_CASE("return whether same day", "bool SameDay(TimeVal time)") {
    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.same_day(date_time_instance.get_milliseconds()) == true);
}

TEST_CASE("return whether same month", "bool SameMonth(TimeVal time)") {
    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.same_month(date_time_instance.get_milliseconds()) == true);
}

TEST_CASE("return whether same year", "bool SameYear(TimeVal time)") {
    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.same_year(date_time_instance.get_milliseconds()) == true);
}

TEST_CASE("return whether same week", "bool SameWeek(TimeVal time)") {
    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.same_week(date_time_instance.get_milliseconds()) == true);
}

TEST_CASE("return time in %Y/%m/%d %H:%M:%S format", "std::string ToString()") {
    time_t control_group;
    time(&control_group);
    char time_buff[32] = {0};
    std::strftime(time_buff, sizeof(time_buff), "%Y/%m/%d %H:%M:%S", localtime(&control_group));

    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.to_string() == (std::string)(time_buff));
}

TEST_CASE("return time in %FT%TZ format", "std::string ToISO8601String()") {
    time_t control_group;
    time(&control_group);
    char time_buff[64] = {0};
    std::strftime(time_buff, sizeof(time_buff), "%FT%TZ", gmtime(&control_group));

    ark::date_time date_time_instance;
    REQUIRE(date_time_instance.to_iso8601_string() == (std::string)(time_buff));
}

TEST_CASE("return parameter year whether leapYear", "static bool IsLeapYear(int year)") {
    auto year = ark::date_time().get_year();
    REQUIRE(ark::date_time::is_leap_year(year) == ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0)));
}

TEST_CASE("return days of month by parameter year and month", "static int GetDaysOfMonth(int year, int month)") {
    auto date_time_provider = ark::date_time();
    auto year = date_time_provider.get_year();
    auto month = date_time_provider.get_month();
    int days_of_month_table[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && date_time_provider.is_leap_year()) {
        days_of_month_table[2] = 29;
    }
    REQUIRE(ark::date_time::get_days_of_month(year, month) == days_of_month_table[month]);
}

TEST_CASE("return y/m/d/h/m/s parameter whether valid",
    "static bool isValid(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int "
    "millisecond = 0)") {
    REQUIRE(ark::date_time::is_valid(1000, 0, 1) == false);
    REQUIRE(ark::date_time::is_valid(10000, 1, 1) == false);
    REQUIRE(ark::date_time::is_valid(1000, 19, 1) == false);
    REQUIRE(ark::date_time::is_valid(1000, 2, 30) == false);
    REQUIRE(ark::date_time::is_valid(1000, 1, 1, 80) == false);
    REQUIRE(ark::date_time::is_valid(1000, 9, 1, 16, 90) == false);
    REQUIRE(ark::date_time::is_valid(1000, 9, 1, 16, 30, 90) == false);
    REQUIRE(ark::date_time::is_valid(1000, 9, 1, 10, 30, 30, 2000) == false);
    REQUIRE(ark::date_time::is_valid(2019, 9, 24, 20, 6, 30, 300) == true);
}

TEST_CASE("return current time", "static TimeVal GetNowTime()") {
    REQUIRE(ark::date_time::get_now_time() ==
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
}

TEST_CASE("return timestamp", "static int GetTimestamp()") {
    REQUIRE(
        ark::date_time::get_timestamp() ==
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

TEST_CASE("inline void swap(AFDateTime& d1, AFDateTime& d2)", "[swap]") {
    ark::date_time date_time_instance;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    ark::date_time date_time_control_group;
    swap(date_time_instance, date_time_control_group);
    REQUIRE(date_time_instance > date_time_control_group);
}
