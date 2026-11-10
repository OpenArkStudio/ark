#pragma once

#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

namespace ark {

template<typename element_type>
class thread_safe_queue {
private:
    std::queue<element_type> data_queue_;
    mutable std::mutex mutex_;

public:
    thread_safe_queue() = default;

    std::size_t size() {
        std::lock_guard guard(mutex_);
        return data_queue_.size();
    }

    void push(element_type new_value) {
        std::lock_guard guard(mutex_);
        data_queue_.push(new_value);
    }

    bool try_pop(element_type& value) {
        std::lock_guard guard(mutex_);
        if (data_queue_.empty()) {
            return false;
        }

        value = data_queue_.front();
        data_queue_.pop();
        return true;
    }

    std::queue<element_type> grab_all() {
        std::queue<element_type> ret;
        {
            std::lock_guard guard(mutex_);
            std::swap(ret, data_queue_);
        }
        return ret;
    }

    bool empty() const {
        std::lock_guard guard(mutex_);
        return data_queue_.empty();
    }
};
} // namespace ark
