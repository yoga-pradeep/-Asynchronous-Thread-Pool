#pragma once

/*
 * TaskQueue.h
 * ===========
 * A thread-safe blocking queue for tasks.
 * Can be used standalone or as a component inside the ThreadPool.
 *
 * Uses a mutex + two condition variables pattern:
 *   - not_empty_cv: producers notify consumers when data arrives
 *   - not_full_cv:  consumers notify producers when space frees up
 */

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>
#include <cstddef>

namespace tpl {

template<typename T>
class TaskQueue {
public:
    explicit TaskQueue(size_t max_size = 0)   // 0 = unbounded
        : max_size_(max_size) {}

    // Push an item (blocks if at capacity)
    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_cv_.wait(lock, [this]{
            return max_size_ == 0 || queue_.size() < max_size_ || closed_;
        });
        if (closed_) return;
        queue_.push(std::move(item));
        not_empty_cv_.notify_one();
    }

    // Try to push without blocking. Returns false if full or closed.
    bool try_push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || (max_size_ > 0 && queue_.size() >= max_size_)) {
            return false;
        }
        queue_.push(std::move(item));
        not_empty_cv_.notify_one();
        return true;
    }

    // Pop an item (blocks until available or closed)
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_cv_.wait(lock, [this]{ return !queue_.empty() || closed_; });
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_cv_.notify_one();
        return item;
    }

    // Pop with timeout. Returns nullopt on timeout or close.
    template<typename Rep, typename Period>
    std::optional<T> pop_for(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_cv_.wait_for(lock, timeout,
                [this]{ return !queue_.empty() || closed_; })) {
            return std::nullopt;
        }
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_cv_.notify_one();
        return item;
    }

    // Close the queue. All blocked pop() calls will unblock and return nullopt.
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    std::queue<T>           queue_;
    size_t                  max_size_;
    bool                    closed_ = false;
};

} // namespace tpl
