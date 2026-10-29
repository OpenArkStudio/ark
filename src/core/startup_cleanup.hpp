// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

namespace ark {

enum class startup_cleanup_result
{
    not_run,
    done,
    unsafe,
    failed
};

using startup_cleanup_action = startup_cleanup_result (*)(void*);

enum class startup_call_result
{
    not_entered,
    entered,
    passed,
    returned_false,
    exception
};

// The most recent call, not a full object/resource inventory. Storage and
// recording never allocate; resource ownership belongs to the separate capsule.
struct startup_call_record
{
    std::array<char, 96> owner{};
    std::array<char, 24> phase{};
    startup_call_result result{startup_call_result::not_entered};

    bool diagnostic_ready{};

    void enter(std::string_view owner_name, std::string_view phase_name) noexcept {
        copy(owner, owner_name);
        copy(phase, phase_name);
        result = startup_call_result::entered;
        diagnostic_ready = false;
    }

private:
    template<std::size_t size>
    static void copy(std::array<char, size>& target, std::string_view value) noexcept {
        const auto count = value.size() < size - 1 ? value.size() : size - 1;
        for (std::size_t index{}; index < count; ++index)
        {
            target[index] = value[index];
        }
        target[count] = '\0';
    }
};

namespace detail {

// Exactly one independently owned resource in this slice. No vector/function
// allocation, incomplete module pointer or deleter invocation on the failure path.
class startup_cleanup_slot
{
public:
    bool prepare(std::string_view owner, std::string_view code_owner, const std::shared_ptr<void>& context,
        startup_cleanup_action action) noexcept {
        if (prepared_ || owner.empty() || owner.size() >= owner_.size() || code_owner.empty() ||
            code_owner.size() >= code_owner_.size() || owner.find(char{}) != std::string_view::npos ||
            code_owner.find(char{}) != std::string_view::npos || !context || !action)
        {
            return false;
        }
        for (std::size_t index{}; index < owner.size(); ++index)
        {
            owner_[index] = owner[index];
        }
        for (std::size_t index{}; index < code_owner.size(); ++index)
        {
            code_owner_[index] = code_owner[index];
        }
        context_ = context;
        action_ = action;
        prepared_ = true;
        return true;
    }

    startup_cleanup_result run() noexcept {
        if (!prepared_ || entered_)
        {
            return result_;
        }
        entered_ = true;
        try
        {
            result_ = action_(context_.get());
            if (result_ == startup_cleanup_result::not_run)
            {
                result_ = startup_cleanup_result::failed;
            }
        }
        catch (...)
        { result_ = startup_cleanup_result::failed; }
        // Keep even a successfully emptied capsule: its deleter/control block
        // may contain DSO code. The launcher exits without unloading anything.
        return result_;
    }

    void disarm() noexcept {
        action_ = nullptr;
        entered_ = true;
        context_.reset();
    }

private:
    std::array<char, 96> owner_{};
    std::array<char, 96> code_owner_{};
    std::shared_ptr<void> context_;
    startup_cleanup_action action_{};
    startup_cleanup_result result_{startup_cleanup_result::not_run};
    bool prepared_{};
    bool entered_{};
};

} // namespace detail
} // namespace ark
