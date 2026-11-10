// Copyright(c) 2019 ARK
// Based on rotating_file_sink
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
//

#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <tuple>

#include <spdlog/details/null_mutex.h>
#include <spdlog/details/synchronous_factory.h>
#include <spdlog/sinks/base_sink.h>

#include "ark/spdlog/sinks/native_file.hpp"

namespace spdlog {

namespace sinks {

//
// Rotating file sink based on size
//
template<typename mutex_type>
class rotating_file_with_date_sink final : public base_sink<mutex_type> {
public:
    rotating_file_with_date_sink(filename_t base_filename, std::size_t max_size, std::size_t max_files);
    static std::tuple<filename_t, filename_t, filename_t> split_by_dir_and_extenstion(const spdlog::filename_t& fname);
    static filename_t calc_filename(const filename_t& filename, std::size_t index);

protected:
    void sink_it_(const details::log_msg& msg) override;
    void flush_() override;

private:
    void rotate();
    bool rename_file(const filename_t& src_filename, const filename_t& target_filename);

    filename_t base_filename_;
    std::size_t max_size_;
    std::size_t max_files_;
    std::size_t current_size_;
    ark::detail::logging_file file_helper_;
};

using rotating_file_with_date_sink_mt = rotating_file_with_date_sink<std::mutex>;
using rotating_file_with_date_sink_st = rotating_file_with_date_sink<details::null_mutex>;

} // namespace sinks

//
// factory functions
//

template<typename factory_type = spdlog::synchronous_factory>
inline std::shared_ptr<logger> rotating_logger_mt(
    const std::string& logger_name, const filename_t& filename, size_t max_file_size, size_t max_files) {
    return factory_type::template create<sinks::rotating_file_with_date_sink_mt>(
        logger_name, filename, max_file_size, max_files);
}

template<typename factory_type = spdlog::synchronous_factory>
inline std::shared_ptr<logger> rotating_logger_st(
    const std::string& logger_name, const filename_t& filename, size_t max_file_size, size_t max_files) {
    return factory_type::template create<sinks::rotating_file_with_date_sink_st>(
        logger_name, filename, max_file_size, max_files);
}
} // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
#include "ark/spdlog/sinks/rotating_file_with_date_sink-inl.h"
#endif
