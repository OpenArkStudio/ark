// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../network/doctest.h"

// Including ark must neither redefine nor undefine externally supplied macros.
#define SPDLOG_STRRCHR(file, separator) "external-strrchr"
#define SPDLOG_FILE_BASENAME(file) "external-basename"
#include <ark/logger.hpp>

static_assert(std::string_view(SPDLOG_STRRCHR("", '/')) == "external-strrchr");
static_assert(std::string_view(SPDLOG_FILE_BASENAME("")) == "external-basename");

namespace {

class captured_logger {
public:
    std::vector<std::pair<spdlog::level::level_enum, std::string>> messages;

    captured_logger()
        : previous_(ark::logger::instance()) {
        current_.set_log_func([this](spdlog::level::level_enum level, const std::string& content) {
            messages.emplace_back(level, content);
        });
        ark::logger::init(&current_);
    }

    ~captured_logger() {
        ark::logger::init(previous_);
    }

private:
    ark::logger* previous_;
    ark::logger current_;
};

} // namespace

TEST_CASE("Logging macros capture caller file and line at every level") {
    captured_logger capture;
    using ark::logger;
    std::array<int, 6> lines;
    lines[0] = __LINE__ + 1;
    ARK_LOG_TRACE("trace");
    lines[1] = __LINE__ + 1;
    ARK_LOG_DEBUG("debug {}", 1);
    lines[2] = __LINE__ + 1;
    ARK_LOG_INFO("info");
    lines[3] = __LINE__ + 1;
    ARK_LOG_WARN("warn {}", 2);
    lines[4] = __LINE__ + 1;
    ARK_LOG_ERROR("error");
    lines[5] = __LINE__ + 1;
    ARK_LOG_CRITICAL("critical {}", 3);
    constexpr std::array<spdlog::level::level_enum, 6> levels = {spdlog::level::trace, spdlog::level::debug,
        spdlog::level::info, spdlog::level::warn, spdlog::level::err, spdlog::level::critical};
    constexpr std::array<const char*, 6> contents = {"trace", "debug 1", "info", "warn 2", "error", "critical 3"};
    REQUIRE(capture.messages.size() == levels.size());
    for (std::size_t index = 0; index < levels.size(); ++index) {
        CHECK(capture.messages[index].first == levels[index]);
        CHECK(capture.messages[index].second ==
              "[logging_source.cpp:" + std::to_string(lines[index]) + "]" + contents[index]);
    }
}

TEST_CASE("Logging macros preserve empty arguments runtime formats and literal braces") {
    captured_logger capture;
    using ark::logger;
    int evaluated = 0;
    const auto runtime_format = [&evaluated]() {
        ++evaluated;
        return "value={:04d} text={} ratio={:.2f}";
    };
    const auto value = [&evaluated]() {
        ++evaluated;
        return 7;
    };
    const int empty_line = __LINE__ + 1;
    ARK_LOG_INFO("");
    const int literal_line = __LINE__ + 1;
    ARK_LOG_DEBUG("{{literal}}");
    const int explicit_empty_line = __LINE__ + 1;
    ARK_LOG_WARN("explicit empty", );
    std::string text = "{owned}";
    const int runtime_line = __LINE__ + 1;
    ARK_LOG_ERROR(runtime_format(), value(), text, 1.25);
    text = "changed";
    REQUIRE(capture.messages.size() == 4);
    CHECK(evaluated == 2);
    CHECK(capture.messages[0].second == "[logging_source.cpp:" + std::to_string(empty_line) + "]");
    CHECK(capture.messages[1].second == "[logging_source.cpp:" + std::to_string(literal_line) + "]{literal}");
    CHECK(
        capture.messages[2].second == "[logging_source.cpp:" + std::to_string(explicit_empty_line) + "]explicit empty");
    CHECK(capture.messages[3].second ==
          "[logging_source.cpp:" + std::to_string(runtime_line) + "]value=0007 text={owned} ratio=1.25");
    CHECK_THROWS_AS(ARK_LOG_INFO("{"), fmt::format_error);
    CHECK(capture.messages.size() == 4);
}

#line 321 "synthetic/forward\\mixed\\caller.cpp"

TEST_CASE("Logging macros reduce synthetic caller paths with both separator kinds") {
    captured_logger capture;
    using ark::logger;
    ARK_LOG_INFO("synthetic");
    REQUIRE(capture.messages.size() == 1);
    CHECK(capture.messages[0].second == "[caller.cpp:325]synthetic");
}
