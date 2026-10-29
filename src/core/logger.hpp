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

#include <filesystem>
#include <source_location>
#include <string_view>

#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/common.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

// custom sinks
#include "ark/spdlog/sinks/date_and_hour_file_sink.h"
//#include "ark/spdlog/sinks/rotating_file_with_date_sink.h"

#include "ark/container/map.hpp"
#include "ark/macros.hpp"
#include "ark/xml.hpp"

#ifndef ARK_PLATFORM_WIN
#include <execinfo.h>
#endif

namespace ark {

namespace detail {

constexpr std::string_view log_basename(std::string_view path) noexcept {
    const auto separator = path.find_last_of("/\\");
    return path.substr(separator == std::string_view::npos ? 0 : separator + 1);
}

constexpr spdlog::source_loc log_source_location(
    const std::source_location& location = std::source_location::current()) noexcept {
    return {log_basename(location.file_name()).data(), static_cast<int>(location.line()), ""};
}

} // namespace detail

#define ARK_LOG_TRACE(my_fmt, ...)                                                                                     \
    logger::instance()->log_t(                                                                                         \
        ::ark::detail::log_source_location(), spdlog::level::trace, my_fmt __VA_OPT__(, ) __VA_ARGS__)
#define ARK_LOG_DEBUG(my_fmt, ...)                                                                                     \
    logger::instance()->log_t(                                                                                         \
        ::ark::detail::log_source_location(), spdlog::level::debug, my_fmt __VA_OPT__(, ) __VA_ARGS__)
#define ARK_LOG_INFO(my_fmt, ...)                                                                                      \
    logger::instance()->log_t(                                                                                         \
        ::ark::detail::log_source_location(), spdlog::level::info, my_fmt __VA_OPT__(, ) __VA_ARGS__)
#define ARK_LOG_WARN(my_fmt, ...)                                                                                      \
    logger::instance()->log_t(                                                                                         \
        ::ark::detail::log_source_location(), spdlog::level::warn, my_fmt __VA_OPT__(, ) __VA_ARGS__)
#define ARK_LOG_ERROR(my_fmt, ...)                                                                                     \
    logger::instance()->log_t(                                                                                         \
        ::ark::detail::log_source_location(), spdlog::level::err, my_fmt __VA_OPT__(, ) __VA_ARGS__)
#define ARK_LOG_CRITICAL(my_fmt, ...)                                                                                  \
    logger::instance()->log_t(                                                                                         \
        ::ark::detail::log_source_location(), spdlog::level::critical, my_fmt __VA_OPT__(, ) __VA_ARGS__);

#define ARK_LOG_OSS(name, content) logger::instance()->log_oss(spdlog::level::info, name, content)

enum class k_log_sink_type : uint8_t
{
    date_and_hour_sink = 0, // default, rotate by hour with specific date path
    size_step_sink,         // rotate by file size, unused for now
    // others
};

class log_conf_data final
{
public:
    std::string output_path{"./binlog"};
    k_log_sink_type sink_type{k_log_sink_type::date_and_hour_sink};
    uint16_t sink_queue_count{1024};

    bool consol{true};
    spdlog::level::level_enum level{spdlog::level::trace};
    std::string pattern{};
};

class log_conf final
{
    shared_object_hashmap<std::string, log_conf_data> conf_datas_;
    std::string default_log_name_{"default"};

public:
    void load_log_conf(std::string const& filename) {
        xml xml(filename);
        auto root_node = xml.get_root_node();

        for (auto logger_node = root_node.find_node("logger"); logger_node.is_valid(); logger_node.next_node())
        {
            // create AFLogConfData
            auto conf_data = std::make_shared<log_conf_data>();
            auto name = logger_node.get_string("name");
            conf_data->sink_queue_count = logger_node.get_uint32("queue_count");
            conf_data->output_path = logger_node.get_string("path");

#ifdef ARK_RUN_MODE_DEBUG
            auto mode_node = logger_node.find_node("debug");
#else
            auto mode_node = logger_node.find_node("release");
#endif //ARK_RUN_MODE_DEBUG

            conf_data->consol = mode_node.get_bool("console");
            conf_data->level = static_cast<spdlog::level::level_enum>(mode_node.get_uint32("level"));
            conf_data->pattern = mode_node.get_string("pattern");

            conf_datas_.insert(name, conf_data);
        }
    }

    std::shared_ptr<log_conf_data> get_conf_data(std::string const& name) {
        return conf_datas_.find_value(name);
    }

    std::string const& get_default() const {
        return default_log_name_;
    }
};

class spdlogger final
{
    std::shared_ptr<log_conf_data> log_conf_{nullptr};
    std::string log_name_{};

    std::shared_ptr<spdlog::logger> logger_{nullptr};
    std::shared_ptr<spdlog::details::thread_pool> tp_{nullptr};

public:
    explicit spdlogger(std::shared_ptr<log_conf_data> conf)
        : log_conf_(conf) {
    }

    ~spdlogger() {
        spdlog::drop(log_name_);
    }

    void init(std::string const& filename) {
        log_name_ = (std::filesystem::path(log_conf_->output_path) / filename).string();
        std::vector<spdlog::sink_ptr> sinks_vec;

#ifdef OPEN_FILE_LOG
        switch (log_conf_->sink_type)
        {
        case k_log_sink_type::date_and_hour_sink:
        {
            auto date_and_hour_sink = std::make_shared<spdlog::sinks::date_and_hour_file_sink_mt>(log_name_);
            sinks_vec.emplace_back(date_and_hour_sink);
        }
        break;
        default:
            ARK_ASSERT_BREAK(0);
            break;
        }
#endif

        if (log_conf_->consol)
        {
#ifdef ARK_PLATFORM_WIN
            auto color_sink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
#else
            auto color_sink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
#endif // ARK_PLATFORM_WIN
            sinks_vec.push_back(color_sink);
        }

        if (log_conf_->sink_queue_count > 0)
        {
            tp_ = std::make_shared<spdlog::details::thread_pool>(log_conf_->sink_queue_count, 1);
            logger_ =
                std::make_shared<spdlog::async_logger>(log_name_, std::begin(sinks_vec), std::end(sinks_vec), tp_);
        }
        else
        {
            logger_ = std::make_shared<spdlog::logger>(log_name_, std::begin(sinks_vec), std::end(sinks_vec));
        }

        logger_->set_pattern(log_conf_->pattern);
        logger_->set_level(log_conf_->level);
        logger_->flush_on(log_conf_->level);

        spdlog::register_logger(logger_);
    }

    void log(spdlog::level::level_enum level, std::string const& content) {
        logger_->log(level, content);
    }
};

using log_functor = std::function<void(spdlog::level::level_enum, std::string const&)>;

class logger final : public singleton<logger>
{
    using logger_t = std::shared_ptr<spdlogger>;

    // singleton
    //inline static AFLogger* singleton_logger_{nullptr};

    // default logger
    logger_t default_logger_{nullptr};

    // oss logger
    logger_t oss_logger_{nullptr};

    // created logger list
    std::unordered_map<bus_id_t, logger_t> loggers_;

    // log function
    log_functor log_func_;

    // log configuration
    std::unique_ptr<log_conf> log_conf_{nullptr};

public:
    ~logger() {
        loggers_.clear();
        //ARK_DELETE(AFLogger::singleton_logger_);
    }

    static void init(logger* instance) {
        if (instance == nullptr)
        {
            singleton<logger>::instance();
            return;
        }

        // Borrow the host logger; its creator controls its lifetime.
        singleton<logger>::set_instance(instance);
    }

    bool init_logger(std::string const& conf_file, std::string const& log_filename) {
        if (log_conf_ == nullptr)
        {
            log_conf_ = std::make_unique<log_conf>();
            log_conf_->load_log_conf(conf_file);
        }

        // init local logger
        auto conf_data = log_conf_->get_conf_data(log_conf_->get_default());
        ARK_ASSERT_RET_VAL(conf_data != nullptr, false);

        default_logger_ = std::make_shared<spdlogger>(conf_data);
        default_logger_->init(log_filename);

        // set log function for local logger
        auto func = std::bind(&logger::log, this, std::placeholders::_1, std::placeholders::_2);
        set_log_func(std::move(func));

        // init oss logger
        auto oss_conf_data = log_conf_->get_conf_data("oss");
        ARK_ASSERT_RET_VAL(oss_conf_data != nullptr, false);

        oss_logger_ = std::make_shared<spdlogger>(oss_conf_data);
        oss_logger_->init(log_filename);

        return true;
    }

    logger_t create_logger(bus_id_t id, std::string const& name, std::string const& filename) {
        auto iter = loggers_.find(id);
        if (iter != loggers_.end())
        {
            return iter->second;
        }

        // find log configuration
        auto log_conf_data = log_conf_->get_conf_data(name);
        if (log_conf_data == nullptr)
        {
            return nullptr;
        }

        // create logger
        auto new_logger = std::make_shared<spdlogger>(log_conf_data);
        new_logger->init(filename);
        loggers_.insert(std::make_pair(id, new_logger));
        return new_logger;
    }

    void remove_logger(bus_id_t id) {
        auto iter = loggers_.find(id);
        if (iter == loggers_.end())
        {
            return;
        }

        loggers_.erase(iter);
    }

    void set_log_func(log_functor&& func) {
        if (func != nullptr)
        {
            log_func_ = func;
        }
        else
        {
            auto func = std::bind(&logger::log, this, std::placeholders::_1, std::placeholders::_2);
            log_func_ = func;
        }
    }

    template<typename... argument_types>
    void log_t(
        spdlog::source_loc&& loc, spdlog::level::level_enum log_level, const char* my_fmt, argument_types&&... args) {
        static const std::string func_line_fmt = "[{}:{}]";
        auto new_fmt = func_line_fmt + my_fmt;
        auto content = ARK_FORMAT(new_fmt, loc.filename, loc.line, std::forward<argument_types>(args)...);
        log_func_(log_level, content);
    }

    void log_oss(spdlog::level::level_enum log_level, const std::string& name, const std::string& content) {
        static const std::string oss_fmt = "{}:{}";
        auto final_content = ARK_FORMAT(oss_fmt, name, content);
        oss_logger_->log(log_level, final_content);
    }

    void log_stack_trace(std::string const& error) {
#ifndef ARK_PLATFORM_WIN
        log(spdlog::level::err, "StackTrace Begin =============================");
        log(spdlog::level::err, error);

        static const uint16_t default_size = 256;
        void* default_data[default_size] = {0};
        auto stack_num = backtrace(default_data, default_size);
        auto stack_trace = backtrace_symbols(default_data, stack_num);
        for (auto i = 0; i < stack_num; i++)
        {
            log(spdlog::level::err, stack_trace[i]);
        }
        free(stack_trace);

        log(spdlog::level::err, "StackTrace End ===============================");
#endif
    }

protected:
    void log(spdlog::level::level_enum log_level, std::string const content) {
        default_logger_->log(log_level, content);
    }
};

} // namespace ark
