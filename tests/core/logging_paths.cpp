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
#include <vector>

#include "../network/doctest.h"

#include <ark/logger.hpp>
#include <ark/spdlog/sinks/rotating_file_with_date_sink.h>

namespace {

using filename_calculator = spdlog::sinks::default_date_and_hour_file_name_calculator;

std::string utf8_path(const std::filesystem::path& file) {
    const auto bytes = file.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

spdlog::filename_t native_filename(const std::filesystem::path& file) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
    return file.wstring();
#else
    return utf8_path(file);
#endif
}

std::filesystem::path native_path(const spdlog::filename_t& filename) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
    return std::filesystem::path(filename);
#else
    return std::filesystem::path(std::u8string(filename.begin(), filename.end()));
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

std::string read_log(const spdlog::filename_t& file) {
    return read_log(native_path(file));
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

TEST_CASE("Logging callback reset restores configured output formatting and level filtering") {
    log_directory files;
    std::string route;
    SUBCASE("direct settings") {
        route = "direct";
    }
#ifdef ARK_LOGGING_CONFIG_TESTS
    SUBCASE("TOML settings") {
        route = "toml";
    }
#endif
#ifdef OPEN_FILE_LOG
    SUBCASE("legacy XML settings") {
        route = "xml";
    }
#endif

    const auto output_directory = files.directory / "default";
    ark::logging_settings settings;
    settings.console = false;
    settings.file = true;
    settings.pattern = "%l|%v";
    settings.level = "warn";
    settings.output_path = output_directory.string();
    ark::logger logger;
    if (route == "xml")
    {
        const auto config = files.directory / "legacy.xml";
        {
            std::ofstream output(config);
            output << "<xml>";
            for (const auto& name : {"default", "oss"})
            {
                output << "<logger name='" << name << "' queue_count='0' path='" << (files.directory / name).string()
                       << "'><debug console='0' level='3' pattern='%l|%v'/>"
                       << "<release console='0' level='3' pattern='%l|%v'/></logger>";
            }
            output << "</xml>";
        }
        REQUIRE(logger.init_logger(config.string(), "callback.log"));
    }
    else
    {
#ifdef ARK_LOGGING_CONFIG_TESTS
        if (route == "toml")
        {
            const auto config = files.directory / "logging.app.toml";
            {
                std::ofstream output(config);
                output << "[application]\n[logging]\nconsole = false\nfile = true\n"
                       << "level = 'warn'\npattern = '%l|%v'\nqueue_count = 0\noutput_path = '"
                       << output_directory.generic_string() << "'\n";
            }
            settings = ark::load_application_manifest(config.string()).logging;
        }
#endif
        REQUIRE(logger.init_logger(settings, "callback.log"));
    }

    logger.log_t({"callback.cpp", 41, ""}, spdlog::level::warn, "initial {{literal}}");
    std::vector<std::pair<spdlog::level::level_enum, std::string>> captured;
    logger.set_log_func(
        [&](spdlog::level::level_enum level, const std::string& content) { captured.emplace_back(level, content); });
    logger.log_t({"callback.cpp", 42, ""}, spdlog::level::trace, "custom {{literal}}");
    logger.set_log_func({});
    logger.log_t({"callback.cpp", 43, ""}, spdlog::level::info, "filtered after reset");
    std::string content = "{owned}";
    logger.log_t({"callback.cpp", 44, ""}, spdlog::level::err, "restored {}", content);
    content.clear();

    REQUIRE(captured.size() == 1);
    CHECK(captured[0].first == spdlog::level::trace);
    CHECK(captured[0].second == "[callback.cpp:42]custom {literal}");
    const auto output = filename_calculator::calc_filename(native_filename(output_directory / "callback.log"));
    CHECK(read_log(output) == std::string("warning|[callback.cpp:41]initial {literal}") +
                                  spdlog::details::os::default_eol + "error|[callback.cpp:44]restored {owned}" +
                                  spdlog::details::os::default_eol);
}

TEST_CASE("Logging legacy XML rejects an empty document and a missing selected mode") {
    log_directory files;
    const auto config = files.directory / std::filesystem::path(u8"无效 配置 😀.xml");
    std::string document;
    std::string expected;
    SUBCASE("empty XML document") {
        expected = "missing root element";
    }
    SUBCASE("logger missing Debug and Release modes") {
        document = "<xml><logger name='default' queue_count='0' path='unused'/></xml>";
#ifdef ARK_RUN_MODE_DEBUG
        expected = "missing debug logger mode";
#else
        expected = "missing release logger mode";
#endif
    }
    {
        std::ofstream output(config, std::ios::binary);
        REQUIRE(output.is_open());
        output << document;
    }
    ark::logger logger;
    try
    {
        logger.init_logger(utf8_path(config), "invalid.log");
        FAIL("malformed legacy logger configuration must throw");
    }
    catch (const std::runtime_error& error)
    {
        const std::string diagnostic = error.what();
        CHECK(diagnostic.find(utf8_path(config)) != std::string::npos);
        CHECK(diagnostic.find(expected) != std::string::npos);
    }
}

TEST_CASE("Logging UTF-8 output directories and filenames create native files with UTF-8 content") {
    log_directory files;
    const auto directory = files.directory / std::filesystem::path(u8"中文 日志 😀");
    const auto filename = std::filesystem::path(u8"子目录 🚀") / std::filesystem::path(u8"服务 𠀋.log");
    const auto base = directory / filename;
    const auto message = utf8_path(std::filesystem::path(u8"日志内容 中文 😀 𠀋 {literal}"));
    std::string route;
    SUBCASE("synchronous settings") {
        route = "sync";
    }
    SUBCASE("asynchronous settings") {
        route = "async";
    }
#ifdef ARK_LOGGING_CONFIG_TESTS
    SUBCASE("TOML output_path") {
        route = "toml";
    }
#endif
#ifdef OPEN_FILE_LOG
    SUBCASE("legacy UTF-8 XML path and filename") {
        route = "xml";
    }
#endif
    {
        ark::logger logger;
        if (route == "xml")
        {
            const auto config = files.directory / std::filesystem::path(u8"日志 配置 😀 𠀋.xml");
            {
                std::ofstream output(config, std::ios::binary);
                REQUIRE(output.is_open());
                output << "<xml>";
                for (const auto& name : {"default", "oss"})
                {
                    const auto path = name == std::string("default") ? directory : directory / "oss";
                    output << "<logger name='" << name << "' queue_count='0' path='" << utf8_path(path)
                           << "'><debug console='0' level='2' pattern='%v'/>"
                           << "<release console='0' level='2' pattern='%v'/></logger>";
                }
                output << "</xml>";
            }
            REQUIRE(logger.init_logger(utf8_path(config), utf8_path(filename)));
        }
        else
        {
            ark::logging_settings settings;
            settings.console = false;
            settings.file = true;
            settings.output_path = utf8_path(directory);
            settings.pattern = "%v";
            settings.queue_count = route == "async" ? 8 : 0;
#ifdef ARK_LOGGING_CONFIG_TESTS
            if (route == "toml")
            {
                const auto config = files.directory / "logging.app.toml";
                {
                    std::ofstream output(config, std::ios::binary);
                    REQUIRE(output.is_open());
                    const auto path = directory.generic_u8string();
                    output << "[application]\n[logging]\nconsole = false\nfile = true\npattern = '%v'\n"
                           << "output_path = '" << std::string(reinterpret_cast<const char*>(path.data()), path.size())
                           << "'\n";
                }
                settings = ark::load_application_manifest(utf8_path(config)).logging;
                CHECK(settings.output_path == utf8_path(directory));
            }
#endif
            settings.loggers["oss"] = settings;
            settings.loggers["oss"].output_path = utf8_path(directory / "oss");
            REQUIRE(logger.init_logger(settings, utf8_path(filename)));
        }
        logger.log_t({"utf8.cpp", 7, ""}, spdlog::level::info, "{}", message);
        logger.log_oss(spdlog::level::info, "utf8", message);
    }
    const auto output = native_path(filename_calculator::calc_filename(native_filename(base)));
    CHECK(output.parent_path().parent_path() == base.parent_path());
    CHECK(output.filename().u8string().starts_with(u8"服务 𠀋_"));
    CHECK(std::filesystem::is_regular_file(output));
    CHECK(read_log(output) == "[utf8.cpp:7]" + message + spdlog::details::os::default_eol);
    const auto oss = directory / "oss" / filename;
    CHECK(read_log(filename_calculator::calc_filename(native_filename(oss))) ==
          "utf8:" + message + spdlog::details::os::default_eol);
}

TEST_CASE("Logging native UTF-8 file sinks preserve append truncation and bounded size rotation") {
    log_directory files;
    const auto base =
        files.directory / std::filesystem::path(u8"中文 路径 😀") / std::filesystem::path(u8"服务 𠀋.log");
    const auto filename = native_filename(base);
    const auto payload = utf8_path(std::filesystem::path(u8"内容 中文 😀 𠀋"));
    const std::string eol = spdlog::details::os::default_eol;
    SUBCASE("hourly append and truncate") {
        const auto output = native_path(filename_calculator::calc_filename(filename));
        std::string expected;
        for (const bool truncate : {false, false, true})
        {
            {
                auto sink = std::make_shared<spdlog::sinks::date_and_hour_file_sink_mt>(filename, truncate);
                spdlog::logger logger("native-hourly", sink);
                logger.set_pattern("%v");
                logger.info("{}", payload);
                logger.flush();
            }
            if (truncate)
            {
                expected.clear();
            }
            expected += payload + eol;
            CHECK(read_log(output) == expected);
        }
    }
    SUBCASE("size rotation removes oldest native file") {
        using sink_type = spdlog::sinks::rotating_file_with_date_sink_mt;
        const auto output = native_path(sink_type::calc_filename(filename, 0));
        {
            auto sink = std::make_shared<sink_type>(filename, payload.size() + 1 + eol.size(), 2);
            spdlog::logger logger("native-size", sink);
            logger.set_pattern("%v");
            for (int i = 0; i < 4; ++i)
            {
                logger.info("{}{}", payload, i);
            }
            logger.flush();
        }
        CHECK(read_log(output) == payload + "3" + eol);
        CHECK(read_log(sink_type::calc_filename(filename, 1)) == payload + "2" + eol);
        CHECK(read_log(sink_type::calc_filename(filename, 2)) == payload + "1" + eol);
        CHECK_FALSE(std::filesystem::exists(native_path(sink_type::calc_filename(filename, 3))));
    }
    SUBCASE("size rotation includes pre-existing native file bytes") {
        using sink_type = spdlog::sinks::rotating_file_with_date_sink_mt;
        const auto output = native_path(sink_type::calc_filename(filename, 0));
        REQUIRE(std::filesystem::create_directories(output.parent_path()));
        {
            std::ofstream existing(output, std::ios::binary);
            REQUIRE(existing.is_open());
            existing << payload << eol;
        }
        {
            auto sink = std::make_shared<sink_type>(filename, payload.size() + eol.size(), 2);
            spdlog::logger logger("native-existing-size", sink);
            logger.set_pattern("%v");
            logger.info("replacement");
            logger.flush();
        }
        CHECK(read_log(output) == "replacement" + eol);
        CHECK(read_log(sink_type::calc_filename(filename, 1)) == payload + eol);
    }
    SUBCASE("native file open failures retain the spdlog exception contract") {
        const auto blocker = files.directory / std::filesystem::path(u8"不是目录 😀");
        {
            std::ofstream file(blocker, std::ios::binary);
            REQUIRE(file.is_open());
            file << "existing file";
        }
        CHECK_THROWS_AS(
            spdlog::sinks::date_and_hour_file_sink_mt(native_filename(blocker / base.filename())), spdlog::spdlog_ex);
        CHECK(read_log(blocker) == "existing file");
    }
}
