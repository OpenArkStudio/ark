// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "ark/net/net_session.hpp"

namespace {

bool count_allocations{};
std::size_t array_allocations{};
std::size_t array_bytes{};
std::size_t object_allocations{};
std::size_t object_bytes{};

struct benchmark_options {
    std::string_view mode{"framing"};
    std::size_t sessions{10000};
    std::size_t iterations{200000};
    std::size_t payload{1024};
    std::size_t chunk{64};
    std::size_t batch{1};
    std::size_t warmup{10000};
    std::size_t initial_buffer{};
};

std::size_t parse_number(std::string_view text, bool allow_zero = false) {
    std::size_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || (!allow_zero && value == 0)) {
        throw std::invalid_argument(allow_zero ? "Expected a nonnegative integer" : "Expected a positive integer");
    }
    return value;
}

benchmark_options parse_options(int argc, char** argv) {
    benchmark_options options;
    for (int i = 1; i < argc; i += 2) {
        const std::string_view key(argv[i]);
        if (i + 1 >= argc) {
            throw std::invalid_argument("Each option requires a value");
        }
        const std::string_view value(argv[i + 1]);
        if (key == "--mode") {
            options.mode = value;
        } else if (key == "--sessions") {
            options.sessions = parse_number(value);
        } else if (key == "--iterations") {
            options.iterations = parse_number(value);
        } else if (key == "--warmup-iterations") {
            options.warmup = parse_number(value, true);
        } else if (key == "--payload") {
            options.payload = parse_number(value);
        } else if (key == "--batch") {
            options.batch = parse_number(value);
        } else if (key == "--initial-buffer") {
            options.initial_buffer = parse_number(value);
        } else if (key == "--chunk") {
            options.chunk = parse_number(value);
        } else {
            throw std::invalid_argument("Unknown option: " + std::string(key));
        }
    }
    if ((options.mode != "sessions" && options.mode != "framing") || options.payload > ark::ark_msg_max_length) {
        throw std::invalid_argument("Use --mode sessions|framing and payloads within ark_msg_max_length");
    }
    if (options.iterations > std::numeric_limits<std::size_t>::max() / options.batch) {
        throw std::invalid_argument("The total message count exceeds the addressable range");
    }
    return options;
}

void check(bool value) {
    if (!value) {
        throw std::runtime_error("Frame content or message count mismatch");
    }
}

void parse_frames(ark::tcp_client_session& session, std::string_view frame, std::string_view body,
    const benchmark_options& options, std::size_t iterations) {
    for (std::size_t i = 0; i < iterations; ++i) {
        for (std::size_t pos = 0; pos < frame.size();) {
            const auto count = std::min(options.chunk, frame.size() - pos);
            session.add_buffer(frame.data() + pos, count);
            check(session.parse_buffer_to_msg());
            pos += count;
        }
        for (std::size_t j = 0; j < options.batch; ++j) {
            ark::net_msg* message{};
            check(session.pop_net_msg(message));
            const bool valid = message->get_msg_id() == 7 && message->get_msg_length() == body.size() &&
                               std::string_view(message->get_msg_data(), message->get_msg_length()) == body;
            ark::net_msg::release(message);
            check(valid);
        }
        ark::net_msg* extra{};
        check(!session.pop_net_msg(extra) && session.get_buffer_len() == 0);
    }
}

double process_cpu_seconds() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        throw std::runtime_error("Cannot read process CPU usage");
    }
    const auto ticks = [](const FILETIME& value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000000.0;
#else
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
}

template<class session_type>
std::unique_ptr<session_type> make_session(
    const std::shared_ptr<ark::s_msg_header>& header, ark::conv_id_t id, std::size_t capacity) {
    if (!capacity) {
        return std::make_unique<session_type>(header, id);
    }
    if constexpr (std::constructible_from<session_type, std::shared_ptr<ark::s_msg_header>, ark::conv_id_t,
                      std::nullptr_t, std::size_t>) {
        return std::make_unique<session_type>(header, id, nullptr, capacity);
    } else {
        throw std::invalid_argument("This ark revision does not support --initial-buffer");
    }
}

void run_benchmark(const benchmark_options& options) {
    auto header = std::make_shared<ark::s_msg_header>();
    header->set_message_id(7);
    header->set_message_length(static_cast<uint32_t>(options.payload));
    const std::string body(options.payload, 'x');
    std::string frame;
    header->serialize_to_string(frame);
    frame += body;
    const auto single = frame;
    for (std::size_t i = 1; i < options.batch; ++i) {
        frame += single;
    }

    std::vector<std::unique_ptr<ark::tcp_client_session>> sessions;
    sessions.reserve(options.mode == "sessions" ? options.sessions : 1);
    if (options.mode == "framing") {
        sessions.push_back(make_session<ark::tcp_client_session>(header, 1, options.initial_buffer));
        parse_frames(*sessions.front(), frame, body, options, options.warmup);
    }
    const auto cpu_start = process_cpu_seconds();
    const auto start = std::chrono::steady_clock::now();
    count_allocations = true;
    if (options.mode == "sessions") {
        for (std::size_t i = 0; i < options.sessions; ++i) {
            sessions.push_back(make_session<ark::tcp_client_session>(header, i + 1, options.initial_buffer));
        }
    } else {
        parse_frames(*sessions.front(), frame, body, options, options.iterations);
    }
    count_allocations = false;
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto cpu = process_cpu_seconds() - cpu_start;
    const auto count = options.mode == "sessions" ? options.sessions : options.iterations * options.batch;
    std::cout << "{\"mode\":\"" << options.mode << "\",\"operations\":" << count
              << ",\"session_size_bytes\":" << sizeof(ark::tcp_client_session)
              << ",\"session_alignment_bytes\":" << alignof(ark::tcp_client_session)
              << ",\"payload_bytes\":" << options.payload << ",\"chunk_bytes\":" << options.chunk
              << ",\"batch_messages\":" << options.batch
              << ",\"initial_buffer_override_bytes\":" << options.initial_buffer
              << ",\"warmup_iterations\":" << (options.mode == "framing" ? options.warmup : 0)
              << ",\"elapsed_seconds\":" << elapsed << ",\"operations_per_second\":" << count / elapsed
              << ",\"cpu_percent\":" << cpu * 100.0 / elapsed << ",\"cpp_array_allocations\":" << array_allocations
              << ",\"cpp_array_requested_bytes\":" << array_bytes
              << ",\"cpp_object_allocations\":" << object_allocations
              << ",\"cpp_object_requested_bytes\":" << object_bytes << ",\"ok\":true}\n";
}

} // namespace

// Count ordinary C++ requests; aligned allocations, queue-internal malloc calls
// and socket buffers are outside these metrics. This program runs on one thread.
void* operator new(std::size_t size) {
    if (void* value = std::malloc(size == 0 ? 1 : size)) {
        if (count_allocations) {
            ++object_allocations;
            object_bytes += size;
        }
        return value;
    }
    throw std::bad_alloc();
}

void operator delete(void* value) noexcept {
    std::free(value);
}

void operator delete(void* value, std::size_t) noexcept {
    std::free(value);
}

void* operator new[](std::size_t size) {
    if (void* value = std::malloc(size == 0 ? 1 : size)) {
        if (count_allocations) {
            ++array_allocations;
            array_bytes += size;
        }
        return value;
    }
    throw std::bad_alloc();
}

void operator delete[](void* value) noexcept {
    std::free(value);
}

void operator delete[](void* value, std::size_t) noexcept {
    std::free(value);
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << "Usage: ark_network_buffer_benchmark --mode sessions|framing\n"
                         "  [--sessions N] [--iterations N] [--payload BYTES] [--chunk BYTES] [--batch N]\n"
                         "  [--warmup-iterations N] [--initial-buffer BYTES]\n";
            return 0;
        }
        run_benchmark(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
