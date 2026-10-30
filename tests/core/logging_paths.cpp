// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "../network/doctest.h"

#include <ark/logger.hpp>

namespace {

using filename_calculator = spdlog::sinks::default_date_and_hour_file_name_calculator;

spdlog::filename_t native_filename(const std::filesystem::path& file) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
    return file.wstring();
#else
    return file.string();
#endif
}

struct filename_case
{
    spdlog::filename_t input;
    spdlog::filename_t directory;
    spdlog::filename_t basename;
    spdlog::filename_t extension;
};

const std::array<filename_case, 12> filename_cases = {{
    {SPDLOG_FILENAME_T("application.log"), {}, SPDLOG_FILENAME_T("application"), SPDLOG_FILENAME_T(".log")},
    {SPDLOG_FILENAME_T("application"), {}, SPDLOG_FILENAME_T("application"), {}},
    {SPDLOG_FILENAME_T("application."), {}, SPDLOG_FILENAME_T("application."), {}},
    {SPDLOG_FILENAME_T(".application"), {}, SPDLOG_FILENAME_T(".application"), {}},
    {SPDLOG_FILENAME_T(".application.log"), {}, SPDLOG_FILENAME_T(".application"), SPDLOG_FILENAME_T(".log")},
    {SPDLOG_FILENAME_T("logs/application.log"), SPDLOG_FILENAME_T("logs/"), SPDLOG_FILENAME_T("application"),
        SPDLOG_FILENAME_T(".log")},
    {SPDLOG_FILENAME_T("logs/application"), SPDLOG_FILENAME_T("logs/"), SPDLOG_FILENAME_T("application"), {}},
    {SPDLOG_FILENAME_T("logs/application."), SPDLOG_FILENAME_T("logs/"), SPDLOG_FILENAME_T("application."), {}},
    {SPDLOG_FILENAME_T("logs/.application"), SPDLOG_FILENAME_T("logs/"), SPDLOG_FILENAME_T(".application"), {}},
    {SPDLOG_FILENAME_T("logs/.application.log"), SPDLOG_FILENAME_T("logs/"), SPDLOG_FILENAME_T(".application"),
        SPDLOG_FILENAME_T(".log")},
    {SPDLOG_FILENAME_T("logs.v1/application"), SPDLOG_FILENAME_T("logs.v1/"), SPDLOG_FILENAME_T("application"), {}},
    {SPDLOG_FILENAME_T("logs.v1/nested/application.log"), SPDLOG_FILENAME_T("logs.v1/nested/"),
        SPDLOG_FILENAME_T("application"), SPDLOG_FILENAME_T(".log")},
}};

spdlog::filename_t expected_filename(const filename_case& entry, const std::tm& time) {
    const auto day =
        fmt::format(SPDLOG_FILENAME_T("{:04d}{:02d}{:02d}"), time.tm_year + 1900, time.tm_mon + 1, time.tm_mday);
    const auto name = fmt::format(SPDLOG_FILENAME_T("{}_{:02d}{}"), entry.basename, time.tm_hour, entry.extension);
    return native_filename(std::filesystem::path(entry.directory) / day / name);
}

class log_directory
{
public:
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("ark-log-path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    log_directory() {
        REQUIRE(std::filesystem::create_directory(directory));
    }

    ~log_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};

class temporary_current_directory
{
public:
    explicit temporary_current_directory(const std::filesystem::path& directory)
        : previous_(std::filesystem::current_path()) {
        std::filesystem::current_path(directory);
    }

    ~temporary_current_directory() {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
    }

private:
    std::filesystem::path previous_;
};

class logger_registration
{
public:
    explicit logger_registration(std::string name)
        : name_(std::move(name)) {
    }

    ~logger_registration() {
        spdlog::drop(name_);
    }

private:
    std::string name_;
};

std::string read_log(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Logging path splitting keeps directories separate from basenames and extensions") {
    for (const auto& entry : filename_cases)
    {
        const auto [directory, basename, extension] = filename_calculator::split_by_dir_and_extenstion(entry.input);
        CHECK(directory == entry.directory);
        CHECK(basename == entry.basename);
        CHECK(extension == entry.extension);
    }
#ifdef _WIN32
    const auto [directory, basename, extension] =
        filename_calculator::split_by_dir_and_extenstion(SPDLOG_FILENAME_T("logs.v1\\nested\\application.log"));
    CHECK(directory == SPDLOG_FILENAME_T("logs.v1\\nested\\"));
    CHECK(basename == SPDLOG_FILENAME_T("application"));
    CHECK(extension == SPDLOG_FILENAME_T(".log"));
#endif
}

TEST_CASE("Logging filename calculation preserves relative paths without creating directories") {
    for (const auto& entry : filename_cases)
    {
        const auto before = spdlog::details::os::localtime();
        const auto actual = filename_calculator::calc_filename(entry.input);
        const auto after = spdlog::details::os::localtime();
        CHECK((actual == expected_filename(entry, before) || actual == expected_filename(entry, after)));
        CHECK_FALSE(std::filesystem::path(actual).is_absolute());
    }
}

TEST_CASE("Logging hourly sink creates nested directories for filenames without an extension") {
    log_directory files;
    const auto input = files.directory / "logs.v1" / "nested" / "application";
    const auto output = filename_calculator::calc_filename(native_filename(input));
    REQUIRE(std::filesystem::path(output).parent_path().parent_path() == input.parent_path());
    {
        auto sink = std::make_shared<spdlog::sinks::date_and_hour_file_sink_mt>(native_filename(input));
        spdlog::logger logger("direct-log-path", sink);
        logger.set_pattern("%v");
        logger.info("message {literal}");
        logger.flush();
    }
    CHECK(read_log(output) == std::string("message {literal}") + spdlog::details::os::default_eol);
}

TEST_CASE("Logging multi-thread factory forwards the supplied filename") {
    log_directory files;
    const auto input = files.directory / "multi" / "application.log";
    const auto name = "ark-factory-mt-" + files.directory.filename().string();
    logger_registration registration(name);
    auto logger = spdlog::date_and_hour_file_sink_mt(name, native_filename(input));
    logger->set_pattern("%v");
    logger->info("multi-thread factory");
    logger->flush();
    CHECK(spdlog::get(name) == logger);
    CHECK(read_log(filename_calculator::calc_filename(native_filename(input))) ==
          std::string("multi-thread factory") + spdlog::details::os::default_eol);
}

TEST_CASE("Logging single-thread factory forwards the supplied filename") {
    log_directory files;
    const auto input = files.directory / "single" / "application.log";
    const auto name = "ark-factory-st-" + files.directory.filename().string();
    logger_registration registration(name);
    auto logger = spdlog::date_and_hour_file_sink_st(name, native_filename(input));
    logger->set_pattern("%v");
    logger->info("single-thread factory");
    logger->flush();
    CHECK(spdlog::get(name) == logger);
    CHECK(read_log(filename_calculator::calc_filename(native_filename(input))) ==
          std::string("single-thread factory") + spdlog::details::os::default_eol);
}

TEST_CASE("Logging configured output directory retains its nested file path") {
    log_directory files;
    auto config = std::make_shared<ark::log_conf_data>();
    config->output_path = (files.directory / "configured").string();
    config->sink_queue_count = 0;
    config->consol = false;
    config->pattern = "%v";
    ark::spdlogger logger(config);
    logger.init((std::filesystem::path("nested") / "application").string());
    const auto input = std::filesystem::path(config->output_path) / "nested" / "application";
    CHECK(spdlog::get(input.string()) != nullptr);
    logger.log(spdlog::level::info, "configured output");
#ifdef OPEN_FILE_LOG
    CHECK(read_log(filename_calculator::calc_filename(native_filename(input))) ==
          std::string("configured output") + spdlog::details::os::default_eol);
#endif
}

TEST_CASE("Logging empty output directory keeps the logger and file relative to the working directory") {
    log_directory files;
    temporary_current_directory current(files.directory);
    auto config = std::make_shared<ark::log_conf_data>();
    config->output_path.clear();
    config->sink_queue_count = 0;
    config->consol = false;
    config->pattern = "%v";
    ark::spdlogger logger(config);
    logger.init("application.log");
    CHECK(spdlog::get("application.log") != nullptr);
    logger.log(spdlog::level::info, "relative output");
#ifdef OPEN_FILE_LOG
    const auto output = filename_calculator::calc_filename(SPDLOG_FILENAME_T("application.log"));
    CHECK_FALSE(std::filesystem::path(output).is_absolute());
    CHECK(read_log(output) == std::string("relative output") + spdlog::details::os::default_eol);
#endif
}

TEST_CASE("embedded logger defaults create only a synchronous console INFO sink") {
    log_directory files;
    temporary_current_directory current(files.directory);
    ark::logger logger;
    REQUIRE(logger.init_logger(ark::logging_settings{}, "default.log"));
    const auto active = spdlog::get("default:" + (std::filesystem::path("./binlog") / "default.log").string());
    REQUIRE(active != nullptr);
    CHECK(active->level() == spdlog::level::info);
    CHECK(active->sinks().size() == 1);
    CHECK(std::dynamic_pointer_cast<spdlog::async_logger>(active) == nullptr);
    CHECK_FALSE(std::filesystem::exists("binlog"));
    logger.log_t(ark::detail::log_source_location(), spdlog::level::info, "default logger works");
    logger.log_oss(spdlog::level::info, "fallback", "works");
    CHECK_FALSE(logger.init_logger(ark::logging_settings{}, "second.log"));
}

TEST_CASE("embedded file logging and named async loggers honor explicit settings") {
    log_directory files;
    ark::logging_settings settings;
    settings.console = false;
    settings.file = true;
    settings.pattern = "%v";
    settings.output_path = (files.directory / "logs with spaces").string();
    settings.level = "warn";
    settings.loggers["oss"] = settings;
    settings.loggers["oss"].queue_count = 8;
    ark::logger logger;
    REQUIRE(logger.init_logger(settings, "nested/application.log"));
    const auto path = std::filesystem::path(settings.output_path) / "nested/application.log";
    const auto active = spdlog::get("default:" + path.string());
    REQUIRE(active != nullptr);
    CHECK(active->sinks().size() == 1);
    CHECK(active->level() == spdlog::level::warn);
    active->warn("explicit file output");
    CHECK(read_log(filename_calculator::calc_filename(native_filename(path))) ==
          std::string("explicit file output") + spdlog::details::os::default_eol);
    const auto oss = spdlog::get("oss:" + path.string());
    CHECK(std::dynamic_pointer_cast<spdlog::async_logger>(oss) != nullptr);
    REQUIRE(logger.create_logger(1, "oss", "other.log") != nullptr);
    CHECK(logger.create_logger(2, "missing", "missing.log") == nullptr);
}

TEST_CASE("failed logger registration preserves the existing logger and default overrides apply") {
    ark::logging_settings settings;
    settings.loggers["default"] = settings;
    settings.loggers["default"].level = "error";
    ark::logger owner;
    REQUIRE(owner.init_logger(settings, "registration.log"));
    const auto active = spdlog::get("default:" + (std::filesystem::path("./binlog") / "registration.log").string());
    REQUIRE(active != nullptr);
    CHECK(active->level() == spdlog::level::err);
    {
        ark::logger duplicate;
        CHECK_THROWS_AS(duplicate.init_logger(settings, "registration.log"), spdlog::spdlog_ex);
    }
    CHECK(spdlog::get("default:" + (std::filesystem::path("./binlog") / "registration.log").string()) == active);
    ark::logger invalid;
    settings.queue_count = 65536;
    CHECK_THROWS_AS(invalid.init_logger(settings, "invalid.log"), std::invalid_argument);
}

TEST_CASE("owned asynchronous logging drains its queue when the wrapper is released") {
    log_directory files;
    ark::logging_settings settings;
    settings.console = false;
    settings.file = true;
    settings.pattern = "%v";
    settings.output_path = files.directory.string();
    settings.queue_count = 8;
    {
        ark::logger logger;
        REQUIRE(logger.init_logger(settings, "drain.log"));
        for (int i = 0; i < 32; ++i)
        {
            logger.log_t(ark::detail::log_source_location(), spdlog::level::info, "async marker {}", i);
        }
    }
    const auto content = read_log(filename_calculator::calc_filename(native_filename(files.directory / "drain.log")));
    CHECK(content.find("async marker 0") != std::string::npos);
    CHECK(content.find("async marker 31") != std::string::npos);
    CHECK(spdlog::get("default:" + (files.directory / "drain.log").string()) == nullptr);
}

TEST_CASE("legacy direct-library XML logger configuration remains explicit and usable") {
    log_directory files;
    const auto config_path = files.directory / "legacy.xml";
    const auto default_dir = files.directory / "legacy-default";
    const auto oss_dir = files.directory / "legacy-oss";
    {
        std::ofstream output(config_path);
        output << "<xml>";
        for (const auto& [name, directory] : {std::pair{"default", default_dir}, std::pair{"oss", oss_dir}})
        {
            output << "<logger name='" << name << "' queue_count='0' path='" << directory.string() << "'>"
                   << "<debug console='1' level='2' pattern='%v'/>"
                   << "<release console='1' level='2' pattern='%v'/></logger>";
        }
        output << "</xml>";
    }
    ark::logger logger;
    REQUIRE(logger.init_logger(config_path.string(), "legacy.log"));
    CHECK(spdlog::get((default_dir / "legacy.log").string()) != nullptr);
    CHECK(spdlog::get((oss_dir / "legacy.log").string()) != nullptr);
    logger.log_t(ark::detail::log_source_location(), spdlog::level::info, "legacy explicit config works");
    logger.log_oss(spdlog::level::info, "legacy", "oss works");
    CHECK_FALSE(logger.init_logger(ark::logging_settings{}, "another.log"));
}
