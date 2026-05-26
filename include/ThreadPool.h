#pragma once

/*
 * ThreadPool.h
 * ============
 * Asynchronous Thread Pool / Task Queue Library
 *
 * A production-grade C++ thread pool that allows submitting
 * arbitrary callable tasks which are executed asynchronously
 * by a pool of background worker threads.
 *
 * Features:
 *  - Fixed-size worker thread pool
 *  - Lock-free-friendly task queue (mutex + condition variable)
 *  - std::future support for retrieving results
 *  - Graceful shutdown with task draining
 *  - Task priority support
 *  - Thread-safe statistics
 */

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>
#include <atomic>
#include <memory>
#include <chrono>
#include <string>

namespace tpl {

// ─── Priority Levels ──────────────────────────────────────────────────────────

enum class Priority : int {
    LOW    = 0,
    NORMAL = 1,
    HIGH   = 2
};

// ─── Internal Task Wrapper ────────────────────────────────────────────────────

struct Task {
    std::function<void()> func;
    Priority              priority;
    std::chrono::steady_clock::time_point enqueued_at;

    Task(std::function<void()> f, Priority p = Priority::NORMAL)
        : func(std::move(f))
        , priority(p)
        , enqueued_at(std::chrono::steady_clock::now())
    {}

    // For the priority queue comparator (higher priority = served first)
    bool operator<(const Task& other) const {
        return static_cast<int>(priority) < static_cast<int>(other.priority);
    }
};

// ─── Pool Statistics ──────────────────────────────────────────────────────────

struct PoolStats {
    size_t   total_submitted  = 0;
    size_t   total_completed  = 0;
    size_t   total_failed     = 0;
    size_t   queue_depth      = 0;
    size_t   active_workers   = 0;
    size_t   total_workers    = 0;
    double   avg_wait_ms      = 0.0;
};

// ─── ThreadPool ───────────────────────────────────────────────────────────────

class ThreadPool {
public:
    /*
     * Construct a thread pool with `num_threads` worker threads.
     * Default: hardware concurrency (number of CPU cores).
     */
    explicit ThreadPool(size_t num_threads = 0);

    /*
     * Destructor: waits for all queued tasks to finish (graceful shutdown).
     */
    ~ThreadPool();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    /*
     * Submit a callable task to the pool.
     * Returns a std::future<ReturnType> to retrieve the result later.
     *
     * Usage:
     *   auto fut = pool.submit([]{ return 42; });
     *   int result = fut.get();  // blocks until the task completes
     *
     * Throws std::runtime_error if the pool is shutting down.
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /*
     * Submit with explicit priority.
     */
    template<typename F, typename... Args>
    auto submit(Priority p, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /*
     * Signal the pool to stop accepting new tasks and wait for
     * all currently queued tasks to finish.
     */
    void shutdown();

    /*
     * Signal immediate stop — running tasks finish, queued tasks are dropped.
     */
    void shutdown_now();

    /*
     * Block the calling thread until all submitted tasks have completed.
     */
    void wait_all();

    // ── Introspection ──────────────────────────────────────────────────────

    size_t    num_threads()    const { return workers_.size(); }
    size_t    queue_size()     const;
    size_t    active_count()   const { return active_count_.load(); }
    bool      is_shutdown()    const { return stop_.load(); }
    PoolStats stats()          const;

private:
    // Worker thread main loop
    void worker_loop();

    // ── Data Members ───────────────────────────────────────────────────────

    std::vector<std::thread>          workers_;
    std::priority_queue<Task>         task_queue_;

    mutable std::mutex                queue_mutex_;
    std::condition_variable           queue_cv_;
    std::condition_variable           idle_cv_;   // signals when all tasks done

    std::atomic<bool>                 stop_       { false };
    std::atomic<bool>                 drain_only_ { false }; // shutdown() mode
    std::atomic<size_t>               active_count_{ 0 };

    // Stats (protected by queue_mutex_)
    mutable std::atomic<size_t>       stat_submitted_ { 0 };
    mutable std::atomic<size_t>       stat_completed_ { 0 };
    mutable std::atomic<size_t>       stat_failed_    { 0 };
    mutable std::atomic<double>       stat_wait_ms_   { 0.0 };
};

// ─── Template Implementations (must live in header) ──────────────────────────

template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    return submit(Priority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto ThreadPool::submit(Priority p, F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using ReturnType = std::invoke_result_t<F, Args...>;

    if (stop_.load() || drain_only_.load()) {
        throw std::runtime_error("ThreadPool: cannot submit — pool is shutting down");
    }

    // Package the callable + args into a shared_ptr to a packaged_task
    // (shared_ptr because std::function requires copyable callables)
    auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<ReturnType> future = task_ptr->get_future();

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.emplace([task_ptr]{ (*task_ptr)(); }, p);
        stat_submitted_.fetch_add(1, std::memory_order_relaxed);
    }

    queue_cv_.notify_one();   // Wake one sleeping worker
    return future;
}

} // namespace tpl
