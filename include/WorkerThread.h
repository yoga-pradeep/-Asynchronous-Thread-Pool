#pragma once

/*
 * WorkerThread.h
 * ==============
 * Represents a single managed worker thread inside the pool.
 * Tracks per-thread statistics for observability.
 */

#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <chrono>

namespace tpl {

class WorkerThread {
public:
    explicit WorkerThread(size_t id, std::function<void()> work_fn)
        : id_(id)
        , tasks_completed_(0)
        , is_busy_(false)
        , thread_(std::move(work_fn))
    {}

    ~WorkerThread() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Non-copyable
    WorkerThread(const WorkerThread&)            = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    void join() {
        if (thread_.joinable()) thread_.join();
    }

    size_t id()               const { return id_; }
    size_t tasks_completed()  const { return tasks_completed_.load(); }
    bool   is_busy()          const { return is_busy_.load(); }

    // Called by the pool's worker_loop
    void mark_busy()  { is_busy_.store(true,  std::memory_order_relaxed); }
    void mark_idle()  { is_busy_.store(false, std::memory_order_relaxed); }
    void inc_tasks()  { tasks_completed_.fetch_add(1, std::memory_order_relaxed); }

    std::string thread_id_str() const {
        std::ostringstream oss;
        oss << thread_.get_id();
        return oss.str();
    }

private:
    size_t              id_;
    std::atomic<size_t> tasks_completed_;
    std::atomic<bool>   is_busy_;
    std::thread         thread_;

    // needed for thread_id_str()
    #include <sstream>
};

} // namespace tpl
