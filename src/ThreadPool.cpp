/*
 * ThreadPool.cpp
 * ==============
 * Implementation of the ThreadPool class.
 *
 * Key synchronization objects:
 *   queue_mutex_  — protects task_queue_ and related state
 *   queue_cv_     — workers sleep on this; notified when a task is pushed
 *   idle_cv_      — wait_all() sleeps here; notified when active_count_ → 0
 *
 * Worker lifecycle:
 *   1. Worker wakes when queue_cv_ fires
 *   2. Checks stop_/drain_only_ conditions
 *   3. Pops highest-priority task
 *   4. Increments active_count_, releases lock
 *   5. Executes task (no lock held — parallelism!)
 *   6. Decrements active_count_, notifies idle_cv_
 *   7. Back to sleep
 */

#include "ThreadPool.h"
#include <iostream>
#include <cassert>

namespace tpl {

// ─── Constructor ──────────────────────────────────────────────────────────────

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;   // sensible fallback
    }

    workers_.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        // Each thread runs worker_loop()
        workers_.emplace_back([this] { worker_loop(); });
    }
}

// ─── Destructor ───────────────────────────────────────────────────────────────

ThreadPool::~ThreadPool() {
    // Graceful shutdown: finish everything in queue then stop
    if (!stop_.load() && !drain_only_.load()) {
        shutdown();
    }
}

// ─── Worker Loop ──────────────────────────────────────────────────────────────

void ThreadPool::worker_loop() {
    while (true) {
        Task current_task([]{}); // placeholder
        bool got_task = false;

        {
            // ── Critical section: wait for work ────────────────────────────
            std::unique_lock<std::mutex> lock(queue_mutex_);

            queue_cv_.wait(lock, [this] {
                // Wake up if:
                //  (a) there's a task in the queue, OR
                //  (b) we've been told to stop (hard stop)
                //  (c) drain_only_ + empty queue → time to exit
                return !task_queue_.empty()
                    || stop_.load()
                    || (drain_only_.load() && task_queue_.empty());
            });

            // Hard stop: exit immediately regardless of queue state
            if (stop_.load()) {
                return;
            }

            // Drain mode: if nothing left, exit
            if (drain_only_.load() && task_queue_.empty()) {
                return;
            }

            if (!task_queue_.empty()) {
                // Pop the highest-priority task
                current_task = std::move(const_cast<Task&>(task_queue_.top()));
                task_queue_.pop();
                got_task = true;
                active_count_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // ── Lock released — now we execute in parallel ─────────────────────

        if (got_task) {
            // Measure wait time for stats
            auto wait_duration = std::chrono::steady_clock::now() - current_task.enqueued_at;
            double wait_ms = std::chrono::duration<double, std::milli>(wait_duration).count();

            // Running average of wait time (approximate; lock-free)
            size_t completed = stat_completed_.load(std::memory_order_relaxed);
            double prev_avg  = stat_wait_ms_.load(std::memory_order_relaxed);
            double new_avg   = (prev_avg * completed + wait_ms) / (completed + 1);
            stat_wait_ms_.store(new_avg, std::memory_order_relaxed);

            // Execute task
            try {
                current_task.func();
                stat_completed_.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                stat_failed_.fetch_add(1, std::memory_order_relaxed);
                std::cerr << "[ThreadPool] Worker caught exception: "
                          << e.what() << "\n";
            } catch (...) {
                stat_failed_.fetch_add(1, std::memory_order_relaxed);
                std::cerr << "[ThreadPool] Worker caught unknown exception\n";
            }

            // ── Notify wait_all() if pool just became fully idle ───────────
            size_t remaining = active_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;

            if (remaining == 0) {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (task_queue_.empty()) {
                    idle_cv_.notify_all();
                }
            }
        }
    } // while(true)
}

// ─── shutdown() — graceful ────────────────────────────────────────────────────

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        drain_only_.store(true, std::memory_order_release);
    }
    queue_cv_.notify_all();   // Wake all workers so they can check drain_only_

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

// ─── shutdown_now() — immediate ───────────────────────────────────────────────

void ThreadPool::shutdown_now() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_.store(true, std::memory_order_release);
        // Clear the pending queue
        while (!task_queue_.empty()) task_queue_.pop();
    }
    queue_cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

// ─── wait_all() ───────────────────────────────────────────────────────────────

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    idle_cv_.wait(lock, [this] {
        return task_queue_.empty()
            && active_count_.load(std::memory_order_acquire) == 0;
    });
}

// ─── Introspection ────────────────────────────────────────────────────────────

size_t ThreadPool::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

PoolStats ThreadPool::stats() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    PoolStats s;
    s.total_submitted = stat_submitted_.load(std::memory_order_relaxed);
    s.total_completed = stat_completed_.load(std::memory_order_relaxed);
    s.total_failed    = stat_failed_.load(std::memory_order_relaxed);
    s.queue_depth     = task_queue_.size();
    s.active_workers  = active_count_.load(std::memory_order_relaxed);
    s.total_workers   = workers_.size();
    s.avg_wait_ms     = stat_wait_ms_.load(std::memory_order_relaxed);
    return s;
}

} // namespace tpl
