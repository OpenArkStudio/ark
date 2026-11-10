// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include "detail/database_awaitable.hpp"
#include "redis/detail/redis_service.hpp"
#if defined(ARKNET_ENABLE_SSL)
#include "mysql/detail/mysql_service.hpp"
#endif

namespace {

using clock_type = std::chrono::steady_clock;
using namespace std::chrono_literals;
using ark::detail::database_awaitable;
using ark::detail::module_task;
using ark::detail::module_task_group;

struct options {
    std::string_view protocol{"redis"};
    std::string_view mode{"callback"};
    std::size_t concurrency{4};
    std::size_t window{16};
    std::size_t payload{64};
    std::size_t operations{10000};
    std::size_t warmup{1000};
    std::size_t update_us{1000};
    std::size_t update_budget{64};
    std::size_t timeout_ms{3000};
    std::size_t deadline_seconds{60};
};

std::size_t number(std::string_view text, std::size_t minimum, std::size_t maximum) {
    std::size_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < minimum || value > maximum) {
        throw std::invalid_argument("Numeric argument is invalid or outside its documented bound");
    }
    return value;
}

options parse(int argc, char** argv) {
    options result;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 == argc) {
            throw std::invalid_argument("Each option requires a value; see --help");
        }
        const std::string_view key(argv[index]), value(argv[index + 1]);
        if (key == "--protocol") {
            result.protocol = value;
        } else if (key == "--mode") {
            result.mode = value;
        } else if (key == "--concurrency") {
            result.concurrency = number(value, 1, 64);
        } else if (key == "--window") {
            result.window = number(value, 1, 256);
        } else if (key == "--payload") {
            result.payload = number(value, 1, 65536);
        } else if (key == "--operations") {
            result.operations = number(value, 1, 1000000);
        } else if (key == "--warmup-operations") {
            result.warmup = number(value, 1, 100000);
        } else if (key == "--update-us") {
            result.update_us = number(value, 0, 1000000);
        } else if (key == "--update-budget") {
            result.update_budget = number(value, 1, 4096);
        } else if (key == "--timeout-ms") {
            result.timeout_ms = number(value, 1, 30000);
        } else if (key == "--deadline-seconds") {
            result.deadline_seconds = number(value, 1, 300);
        } else {
            throw std::invalid_argument("Unknown option; credentials must be supplied through environment variables");
        }
    }
    if ((result.protocol != "redis" && result.protocol != "mysql") ||
        (result.mode != "callback" && result.mode != "coroutine") || result.concurrency > result.window ||
        result.operations < result.window || result.warmup < result.window) {
        throw std::invalid_argument(
            "Use redis|mysql, callback|coroutine and concurrency <= window <= warmup/operations");
    }
    return result;
}

std::string environment(const char* name, bool allow_empty = false) {
    const auto* value = std::getenv(name);
    if (!value || (!allow_empty && !*value)) {
        throw std::invalid_argument(std::string("Required environment variable: ") + name);
    }
    return value;
}

std::string binary_payload(std::size_t size) {
    std::string value(size, '\0');
    for (std::size_t index = 0; index < size; ++index) {
        value[index] = static_cast<char>(index % 256);
    }
    return value;
}

double cpu_seconds() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        throw std::runtime_error("Cannot read process CPU time");
    }
    const auto ticks = [](FILETIME value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000000.0;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage)) {
        throw std::runtime_error("Cannot read process CPU time");
    }
    return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
           static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
#endif
}

std::uint64_t peak_rss_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS memory{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        throw std::runtime_error("Cannot read process resident memory");
    }
    return memory.PeakWorkingSetSize;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage)) {
        throw std::runtime_error("Cannot read process resident memory");
    }
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}

double percentile(const std::vector<double>& sorted, std::size_t percent) {
    if (sorted.empty()) {
        return 0;
    }
    const auto rank = (sorted.size() * percent + 99) / 100;
    return sorted[rank - 1];
}

struct errors {
    std::size_t native{}, exception{}, validation{}, admission{}, task{}, deadline{};

    bool any() const {
        return native || exception || validation || admission || task || deadline;
    }
};

std::size_t input_budget(const options& config) {
    // Includes owned command strings/vector capacity and the SQL hex encoding.
    return config.window * (config.payload * 2 + 4096);
}

struct redis_workload {
    using result_type = arknet::redis::result;
    using service_type = ark::detail::redis_service;
    const std::string payload;
    service_type service;

    static arknet::redis::pool_options pool_options(const options& config) {
        arknet::redis::pool_options pool;
        pool.connection.host = environment("ARK_DATABASE_REDIS_HOST");
        pool.connection.port = std::to_string(number(environment("ARK_DATABASE_REDIS_PORT"), 1, 65535));
        if (const auto* user = std::getenv("ARK_DATABASE_REDIS_USER")) {
            pool.connection.username = user;
        }
        if (const auto* password = std::getenv("ARK_DATABASE_REDIS_PASSWORD")) {
            pool.connection.password = password;
        }
        pool.max_connections = config.concurrency;
        pool.max_waiters = config.window;
        pool.acquire_timeout = std::chrono::milliseconds(config.timeout_ms);
        return pool;
    }

    explicit redis_workload(const options& config)
        : payload(binary_payload(config.payload))
        , service(pool_options(config), config.window, input_budget(config)) {
    }

    template<class Handler>
    std::uint64_t start(Handler completion, std::chrono::milliseconds timeout) {
        return service.start(
            {"ECHO", payload},
            [completion = std::move(completion)](ark::detail::redis_service_result result) mutable {
                if (auto* failure = std::get_if<std::exception_ptr>(&result)) {
                    completion(*failure, {});
                } else {
                    completion({}, std::get<result_type>(std::move(result)));
                }
            },
            timeout);
    }

    bool success(const result_type& result) const {
        return bool(result);
    }

    bool valid(const result_type& result) const {
        return result.replies.size() == 1 && result.replies[0].type == arknet::redis::value_type::bulk_string &&
               result.replies[0].text == payload;
    }
};

#if defined(ARKNET_ENABLE_SSL)
struct mysql_workload {
    using result_type = ark::detail::mysql_service_reply;
    using service_type = ark::detail::mysql_service;
    const std::string payload;
    const std::string query;
    service_type service;

    static arknet::mysql::pool_options pool_options(const options& config) {
        arknet::mysql::pool_options pool;
        pool.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
        pool.connection.port = static_cast<std::uint16_t>(number(environment("ARK_DATABASE_MYSQL_PORT"), 1, 65535));
        pool.connection.username = environment("ARK_DATABASE_MYSQL_USER");
        pool.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD", true);
        pool.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
        pool.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
        pool.max_connections = config.concurrency;
        pool.max_waiters = config.window;
        pool.acquire_timeout = std::chrono::milliseconds(config.timeout_ms);
        return pool;
    }

    static std::string select(std::string_view payload) {
        constexpr std::string_view digits = "0123456789abcdef";
        std::string sql = "SELECT X'";
        sql.reserve(payload.size() * 2 + 10);
        for (const unsigned char byte : payload) {
            sql += digits[byte >> 4];
            sql += digits[byte & 15];
        }
        return sql + "'";
    }

    explicit mysql_workload(const options& config)
        : payload(binary_payload(config.payload))
        , query(select(payload))
        , service(pool_options(config), config.window, input_budget(config)) {
    }

    template<class Handler>
    std::uint64_t start(Handler completion, std::chrono::milliseconds timeout) {
        return service.start_query(
            query,
            [completion = std::move(completion)](ark::detail::mysql_service_result result) mutable {
                if (auto* failure = std::get_if<std::exception_ptr>(&result)) {
                    completion(*failure, {});
                } else {
                    completion({}, std::get<result_type>(std::move(result)));
                }
            },
            timeout);
    }

    bool success(const result_type& result) const {
        return bool(result.result) && !result.statement_close && !result.statement_close_exception;
    }

    bool valid(const result_type& result) const {
        if (result.result.columns.size() != 1 || result.result.rows.size() != 1 || result.result.rows[0].size() != 1) {
            return false;
        }
        const auto* value = std::get_if<std::string>(&result.result.rows[0][0]);
        return value && *value == payload;
    }
};
#endif

template<class Workload>
class benchmark {
    using result_type = typename Workload::result_type;

    struct slot {
        std::optional<result_type> previous;
        clock_type::time_point submitted;
        bool active{};
    };

    options config_;
    Workload workload_;
    std::shared_ptr<arknet::manual_runtime> runtime_ = std::make_shared<arknet::manual_runtime>();
    module_task_group tasks_;
    database_awaitable<result_type> bridge_;
    std::vector<slot> slots_;
    std::vector<double> latencies_;
    errors errors_;
    std::size_t started_{}, completed_{}, successful_{}, active_{};
    std::size_t target_{};
    bool measured_{}, stopping_{};

    bool begin(std::size_t index) {
        if (stopping_ || errors_.any() || started_ == target_) {
            return false;
        }
        ++started_;
        ++active_;
        slots_[index].active = true;
        slots_[index].submitted = clock_type::now();
        return true;
    }

    void receive(std::size_t index, std::exception_ptr failure, result_type result) {
        auto& current = slots_[index];
        // Timestamping and owning-result checks are identical in both modes.
        const auto delivered = clock_type::now();
        if (!current.active) {
            ++errors_.validation;
            return;
        }
        current.active = false;
        --active_;
        ++completed_;
        if (measured_) {
            latencies_.push_back(std::chrono::duration<double, std::micro>(delivered - current.submitted).count());
        }
        if (current.previous && !workload_.valid(*current.previous)) {
            ++errors_.validation;
        }
        if (failure) {
            ++errors_.exception;
        } else if (!workload_.success(result)) {
            ++errors_.native;
        } else if (!workload_.valid(result)) {
            ++errors_.validation;
        } else {
            ++successful_;
            current.previous = std::move(result);
        }
    }

    template<class Handler>
    std::uint64_t start(Handler handler) {
        const auto id = workload_.start(std::move(handler), std::chrono::milliseconds(config_.timeout_ms));
        if (!id) {
            ++errors_.admission;
        }
        return id;
    }

    void refill() {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            if (!slots_[index].active && begin(index)) {
                try {
                    start([this, index](std::exception_ptr error, result_type result) {
                        receive(index, std::move(error), std::move(result));
                    });
                } catch (...) {
                    receive(index, std::current_exception(), {});
                }
            }
        }
    }

    module_task<> sequence(std::size_t index) {
        while (begin(index)) {
            try {
                auto result =
                    co_await bridge_.wait([this](auto completion) { return this->start(std::move(completion)); },
                        [this](std::uint64_t id) { workload_.service.cancel(id); });
                receive(index, {}, std::move(result));
            } catch (...) {
                receive(index, std::current_exception(), {});
            }
        }
    }

    void tick() {
        workload_.service.update(config_.update_budget);
        if (config_.mode == "coroutine") {
            bridge_.dispatch(config_.update_budget);
            tasks_.poll(config_.update_budget);
        } else if (!stopping_) {
            // The service retires its reservation after returning from its
            // callback. Refill outside update(), without an extra request queue.
            refill();
        }
    }

    void pause(clock_type::time_point& next) const {
        if (config_.update_us) {
            next = std::max(next + std::chrono::microseconds(config_.update_us), clock_type::now());
            std::this_thread::sleep_until(next);
        }
    }

    void cancel_and_drain() noexcept {
        try {
            stopping_ = true;
            workload_.service.stop_accepting();
            bridge_.stop_accepting();
            tasks_.close();
            workload_.service.cancel_all();
            const auto deadline = clock_type::now() + std::chrono::milliseconds(config_.timeout_ms) + 10s;
            while (active_ || tasks_.pending() || bridge_.pending()) {
                tick();
                if (clock_type::now() >= deadline) {
                    throw std::runtime_error("drain deadline");
                }
                std::this_thread::sleep_for(100us);
            }
        } catch (...) {
            // Never destroy suspended frames or report a fictitious drain.
            std::cerr << "Database benchmark failed to drain real terminal completions\n" << std::flush;
            std::_Exit(2);
        }
    }

    void phase(std::size_t count, bool measured) {
        started_ = completed_ = successful_ = 0;
        target_ = count;
        measured_ = measured;
        try {
            if (config_.mode == "coroutine") {
                for (std::size_t index = 0; index < slots_.size(); ++index) {
                    if (!tasks_.start(sequence(index), [this](std::exception_ptr error) {
                            if (error) {
                                ++errors_.task;
                            }
                        })) {
                        ++errors_.admission;
                        break;
                    }
                }
            } else {
                refill();
            }
            const auto deadline = clock_type::now() + std::chrono::seconds(config_.deadline_seconds);
            auto next = clock_type::now();
            while (active_ || tasks_.pending() || (!errors_.any() && completed_ < target_)) {
                tick();
                if (clock_type::now() >= deadline) {
                    ++errors_.deadline;
                }
                if (errors_.any()) {
                    cancel_and_drain();
                    return;
                }
                if (active_ || tasks_.pending() || completed_ < target_) {
                    pause(next);
                }
            }
        } catch (...) {
            ++errors_.exception;
            cancel_and_drain();
        }
    }

public:
    explicit benchmark(options config)
        : config_(config)
        , workload_(config)
        , tasks_(runtime_, config.window)
        , bridge_(runtime_, config.window)
        , slots_(config.window) {
        latencies_.reserve(config.operations);
    }

    int run() {
        phase(config_.warmup, false);
        const bool warmup_ok =
            !errors_.any() && completed_ == config_.warmup && !active_ && !tasks_.pending() && !bridge_.pending();
        // Native pool/runtime construction and fully completed warmup precede
        // the measured interval. Sorting, RSS inspection and shutdown follow it.
        const auto cpu_start = cpu_seconds();
        const auto wall_start = clock_type::now();
        if (warmup_ok) {
            phase(config_.operations, true);
        }
        const double seconds = std::chrono::duration<double>(clock_type::now() - wall_start).count();
        const double cpu = cpu_seconds() - cpu_start;
        const auto memory = peak_rss_bytes();
        workload_.service.stop_accepting();
        bridge_.stop_accepting();
        tasks_.close();
        workload_.service.stop();
        const auto diagnostics = workload_.service.diagnostics();
        if (diagnostics.cancellation_submission || diagnostics.lease_return || bridge_.cancellation_exception()) {
            ++errors_.exception;
        }
        const bool valid =
            warmup_ok && !errors_.any() && completed_ == config_.operations && latencies_.size() == config_.operations;
        std::sort(latencies_.begin(), latencies_.end());
        std::cout
            << std::fixed << std::setprecision(3) << "{\"schema\":\"ark-private-database-benchmark-v1\",\"protocol\":\""
            << config_.protocol << "\",\"mode\":\"" << config_.mode << "\",\"asio\":\""
#if defined(ARKNET_USE_BOOST_ASIO) && ARKNET_USE_BOOST_ASIO
            << "boost"
#else
            << "standalone"
#endif
            << "\",\"valid\":" << (valid ? "true" : "false")
            << ",\"warmup_completed\":" << (warmup_ok ? config_.warmup : completed_)
            << ",\"requested\":" << config_.operations << ",\"completed\":" << (warmup_ok ? completed_ : 0)
            << ",\"successful\":" << (warmup_ok ? successful_ : 0) << ",\"concurrency\":" << config_.concurrency
            << ",\"window\":" << config_.window << ",\"payload_bytes\":" << config_.payload
            << ",\"update_us\":" << config_.update_us << ",\"update_budget\":" << config_.update_budget
            << ",\"timeout_ms\":" << config_.timeout_ms << ",\"deadline_seconds\":" << config_.deadline_seconds
            << ",\"wall_seconds\":" << std::setprecision(9) << (warmup_ok ? seconds : 0) << std::setprecision(3)
            << ",\"throughput_ops_per_second\":" << (warmup_ok ? successful_ / seconds : 0)
            << ",\"latency_p50_us\":" << percentile(latencies_, 50)
            << ",\"latency_p99_us\":" << percentile(latencies_, 99) << ",\"latency_samples\":" << latencies_.size()
            << ",\"process_cpu_percent\":" << (warmup_ok ? cpu / seconds * 100 : 0) << ",\"peak_rss_bytes\":" << memory
            << ",\"rss_scope\":\"whole process lifetime including startup and warmup\""
            << ",\"latency_scope\":\"owner submission to owner result delivery, including pool acquire/reset wait, "
               "native IO, database RTT and owner polling\""
            << ",\"measurement_scope\":\"wall/CPU include submissions, delivery, owning validation and "
               "closed-loop refill; process CPU includes all client threads and excludes the external database\""
            << ",\"errors\":{\"native\":" << errors_.native << ",\"exception\":" << errors_.exception
            << ",\"validation\":" << errors_.validation << ",\"admission\":" << errors_.admission
            << ",\"task\":" << errors_.task << ",\"deadline\":" << errors_.deadline << "}}\n";
        return valid ? 0 : 1;
    }
};

void self_test() {
    const auto bytes = binary_payload(513);
    if (bytes.size() != 513 || bytes[0] != '\0' || static_cast<unsigned char>(bytes[255]) != 255 ||
        bytes[256] != '\0' || percentile({1, 2, 3, 4}, 50) != 2 || percentile({1, 2, 3, 4}, 99) != 4) {
        throw std::runtime_error("Binary payload or nearest-rank percentile self-test failed");
    }
    const std::vector<double> hundred = [] {
        std::vector<double> result;
        for (unsigned value = 1; value <= 100; ++value) {
            result.push_back(value);
        }
        return result;
    }();
    if (percentile(hundred, 50) != 50 || percentile(hundred, 99) != 99 || !peak_rss_bytes() || cpu_seconds() < 0) {
        throw std::runtime_error("Resource/percentile self-test failed");
    }
    std::cout << "Database benchmark local self-tests passed\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
            self_test();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "ark_database_benchmark --protocol redis|mysql --mode callback|coroutine\n"
                         "  --concurrency 1..64 --window 1..256 --payload 1..65536\n"
                         "  --operations 1..1000000 --warmup-operations 1..100000\n"
                         "  --update-us 0..1000000 --update-budget 1..4096\n"
                         "  --timeout-ms 1..30000 --deadline-seconds 1..300\n"
                         "Require concurrency <= window <= operations and warmup-operations.\n"
                         "Credentials/endpoints: ARK_DATABASE_REDIS_* or ARK_DATABASE_MYSQL_* environment only.\n"
                         "See benchmarks/DATABASE.md for TLS, measurement scope and bounded smoke checks.\n";
            return 0;
        }
        const auto config = parse(argc, argv);
        if (config.protocol == "redis") {
            return benchmark<redis_workload>(config).run();
        }
#if defined(ARKNET_ENABLE_SSL)
        return benchmark<mysql_workload>(config).run();
#else
        throw std::invalid_argument("MySQL benchmark requires ARKNET_ENABLE_SSL=ON");
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
