#pragma once

#ifndef SPDLOG_H
#include "spdlog/spdlog.h"
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <tuple>

#include "spdlog/details/null_mutex.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/sinks/base_sink.h"

#include "ark/spdlog/sinks/native_file.hpp"

namespace spdlog {

namespace sinks {

/*
 * Default generator of daily log file names.
 */
struct default_date_and_hour_file_name_calculator {
    //
    // return file pre-dir and name and its extension:
    //
    // "mylog.txt" => ("", "mylog", ".txt")
    // "mylog" => ("", "mylog", "")
    // "mylog." => ("", "mylog.", "")
    // "/dir1/dir2/mylog.txt" => ("/dir1/dir2/", "mylog", ".txt")
    //
    // the starting dot in filenames is ignored (hidden files):
    //
    // ".mylog" => ("", ".mylog". "")
    // "my_folder/.mylog" => ("my_folder/", ".mylog", "")
    // "my_folder/.mylog.txt" => ("my_folder/", ".mylog", ".txt")
    static std::tuple<filename_t, filename_t, filename_t> split_by_dir_and_extenstion(const spdlog::filename_t& fname) {
        const auto folder_index = fname.find_last_of(details::os::folder_seps_filename);
        const auto basename_index = folder_index == filename_t::npos ? 0 : folder_index + 1;
        const auto directory = fname.substr(0, basename_index);
        const auto basename = fname.substr(basename_index);
        const auto ext_index = basename.rfind('.');

        if (ext_index == filename_t::npos || ext_index == 0 || ext_index == basename.size() - 1) {
            return std::make_tuple(directory, basename, filename_t{});
        }

        return std::make_tuple(directory, basename.substr(0, ext_index), basename.substr(ext_index));
    }

    // Create filename for the form pre_dir/YYYYMMDD/filename_hh.ext.
    static filename_t calc_filename(const filename_t& filename) {
        std::tm tm = spdlog::details::os::localtime();
        filename_t pre_dir, basename, ext;
        std::tie(pre_dir, basename, ext) = split_by_dir_and_extenstion(filename);
        const auto day =
            fmt::format(SPDLOG_FILENAME_T("{:04d}{:02d}{:02d}"), tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        const auto name = fmt::format(SPDLOG_FILENAME_T("{}_{:02d}{}"), basename, tm.tm_hour, ext);
        const auto path = ark::detail::logging_native_path(pre_dir) / ark::detail::logging_native_path(day) /
                          ark::detail::logging_native_path(name);
        return ark::detail::logging_filename(path);
    }
};

/*
 * Rotating file sink based on date and hour. rotates at Hour:00:00
 * Target path is Date/filename_hour.ext
 */
template<class mutex_type, class file_name_calculator_type = default_date_and_hour_file_name_calculator>
class date_and_hour_file_sink final : public base_sink<mutex_type> {
public:
    // create daily file sink which rotates on given time
    date_and_hour_file_sink(filename_t base_filename, bool truncate = false)
        : base_filename_(std::move(base_filename))
        , truncate_(truncate) {
        auto filename = file_name_calculator_type::calc_filename(base_filename_);
        file_helper_.open(filename, truncate_);
        rotation_tp_ = next_rotation_tp();
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
#ifdef SPDLOG_NO_DATETIME
        auto time = log_clock::now();
#else
        auto time = msg.time;
#endif
        bool should_rotate = time >= rotation_tp_;
        if (should_rotate) {
            auto filename = file_name_calculator_type::calc_filename(base_filename_);
            file_helper_.open(filename, truncate_);
            rotation_tp_ = next_rotation_tp();
        }

        memory_buf_t formatted;
        base_sink<mutex_type>::formatter_->format(msg, formatted);
        file_helper_.write(formatted);
    }

    void flush_() override {
        file_helper_.flush();
    }

private:
    log_clock::time_point next_rotation_tp() {
        auto now = log_clock::now();
        now += std::chrono::hours(1);
        time_t tnow = log_clock::to_time_t(now);
        tm date = spdlog::details::os::localtime(tnow);
        date.tm_min = 0;
        date.tm_sec = 0;
        auto rotation_time = log_clock::from_time_t(std::mktime(&date));
        return rotation_time;
    }

    filename_t base_filename_;
    log_clock::time_point rotation_tp_;
    ark::detail::logging_file file_helper_;
    bool truncate_;
};

using date_and_hour_file_sink_mt = date_and_hour_file_sink<std::mutex>;
using date_and_hour_file_sink_st = date_and_hour_file_sink<details::null_mutex>;

} // namespace sinks

//
// factory functions
//

template<typename factory_type = spdlog::synchronous_factory>
inline std::shared_ptr<logger> date_and_hour_file_sink_mt(const std::string& logger_name, const filename_t& filename) {
    return factory_type::template create<sinks::date_and_hour_file_sink_mt>(logger_name, filename);
}

template<typename factory_type = spdlog::synchronous_factory>
inline std::shared_ptr<logger> date_and_hour_file_sink_st(const std::string& logger_name, const filename_t& filename) {
    return factory_type::template create<sinks::date_and_hour_file_sink_st>(logger_name, filename);
}

} // namespace spdlog
