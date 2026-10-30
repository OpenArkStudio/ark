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

inline constexpr std::size_t startup_cleanup_capacity = 16;

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

// Preallocated independent owners, enrolled before acquisition. Failure cleanup
// retains every capsule and its DSO deleter code until the launcher's exit.
class startup_cleanup_registry
{
public:
    bool prepare(std::string_view owner, std::string_view code_owner, const std::shared_ptr<void>& context,
        startup_cleanup_action action) noexcept {
        if (entered_ || count_ == entries_.size() || !valid_key(owner) || !valid_key(code_owner) || !context || !action)
        {
            return false;
        }
        for (std::size_t index{}; index < count_; ++index)
        {
            if (owner == entries_[index].owner.data())
            {
                return false;
            }
        }
        auto& entry = entries_[count_];
        copy(entry.owner, owner);
        copy(entry.code_owner, code_owner);
        entry.context = context;
        entry.action = action;
        ++count_;
        return true;
    }

    startup_cleanup_result run() noexcept {
        if (entered_)
        {
            return result_;
        }
        entered_ = true;
        for (std::size_t index = count_; index > 0; --index)
        {
            auto& entry = entries_[index - 1];
            try
            {
                result_ = entry.action(entry.context.get());
                if (result_ != startup_cleanup_result::done && result_ != startup_cleanup_result::unsafe &&
                    result_ != startup_cleanup_result::failed)
                {
                    result_ = startup_cleanup_result::failed;
                }
            }
            catch (...)
            { result_ = startup_cleanup_result::failed; }
            if (result_ != startup_cleanup_result::done)
            {
                // An incomplete newer resource can still borrow an older one.
                // Do not release older owners or retry any action after failure.
                break;
            }
        }
        return result_;
    }

    void disarm() noexcept {
        entered_ = true;
        for (std::size_t index{}; index < count_; ++index)
        {
            entries_[index].action = nullptr;
            entries_[index].context.reset();
        }
    }

private:
    struct entry
    {
        std::array<char, 96> owner{};
        std::array<char, 96> code_owner{};
        std::shared_ptr<void> context;
        startup_cleanup_action action{};
    };

    static bool valid_key(std::string_view key) noexcept {
        return !key.empty() && key.size() < 96 && key.find(char{}) == std::string_view::npos;
    }

    static void copy(std::array<char, 96>& target, std::string_view value) noexcept {
        for (std::size_t index{}; index < value.size(); ++index)
        {
            target[index] = value[index];
        }
    }

    std::array<entry, startup_cleanup_capacity> entries_{};
    std::size_t count_{};
    startup_cleanup_result result_{startup_cleanup_result::not_run};
    bool entered_{};
};

} // namespace detail
} // namespace ark
