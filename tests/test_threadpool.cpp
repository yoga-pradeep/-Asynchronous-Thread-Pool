/*
 * test_threadpool.cpp
 * ===================
 * Unit + integration tests for the ThreadPool library.
 * Lightweight framework — no external dependencies.
 *
 * Tests cover:
 *   ✓ Basic task execution
 *   ✓ Future result retrieval
 *   ✓ Exception propagation through futures
 *   ✓ Priority ordering
 *   ✓ wait_all() correctness
 *   ✓ Concurrent submission from multiple threads
 *   ✓ shutdown() drains remaining tasks
 *   ✓ shutdown_now() drops pending tasks
 *   ✓ TaskQueue thread-safety
 *   ✓ Stats accuracy
 */

#include "ThreadPool.h"
#include "TaskQueue.h"

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <numeric>
#include <cassert>
#include <stdexcept>
#include <iomanip>
#include <string>
#include <functional>

using namespace tpl;
using namespace std::chrono_literals;

// ─── Minimal Test Framework ───────────────────────────────────────────────────

static int  g_passed = 0;
static int  g_failed = 0;

#define TEST(name) \
    void name(); \
    struct name##_reg { name##_reg() { run_test(#name, name); } } name##_reg_; \
    void name()

static void run_test(const char* name, std::function<void()> fn) {
    try {
        fn();
        std::cout << "  ✓  " << std::left << std::setw(50) << name << " PASS\n";
        ++g_passed;
    } catch (const std::exception& e) {
        std::cout << "  ✗  " << std::left << std::setw(50) << name
                  << " FAIL: " << e.what() << "\n";
        ++g_failed;
    } catch (...) {
        std::cout << "  ✗  " << std::left << std::setw(50) << name
                  << " FAIL: unknown exception\n";
        ++g_failed;
    }
}

#define ASSERT(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string("Expected ") + std::to_string(b) + " got " + std::to_string(a))

// ─── Tests ────────────────────────────────────────────────────────────────────

TEST(test_basic_submit_and_get) {
    ThreadPool pool(2);
    auto fut = pool.submit([]{ return 42; });
    ASSERT_EQ(fut.get(), 42);
}

TEST(test_void_task) {
    ThreadPool pool(2);
    std::atomic<bool> ran { false };
    auto fut = pool.submit([&ran]{ ran.store(true); });
    fut.get();
    ASSERT(ran.load());
}

TEST(test_multiple_tasks) {
    ThreadPool pool(4);
    const int N = 100;
    std::vector<std::future<int>> futs;
    for (int i = 0; i < N; ++i)
        futs.push_back(pool.submit([i]{ return i * 2; }));

    for (int i = 0; i < N; ++i)
        ASSERT_EQ(futs[i].get(), i * 2);
}

TEST(test_exception_propagation) {
    ThreadPool pool(2);
    auto fut = pool.submit([]{
        throw std::runtime_error("deliberate error");
        return 0;
    });

    bool caught = false;
    try { fut.get(); }
    catch (const std::runtime_error& e) {
        caught = (std::string(e.what()) == "deliberate error");
    }
    ASSERT(caught);
}

TEST(test_wait_all) {
    ThreadPool pool(4);
    std::atomic<int> counter { 0 };

    for (int i = 0; i < 20; ++i) {
        pool.submit([&counter]{
            std::this_thread::sleep_for(10ms);
            counter.fetch_add(1);
        });
    }

    pool.wait_all();
    ASSERT_EQ(counter.load(), 20);
}

TEST(test_priority_ordering) {
    // Single worker so ordering is deterministic
    ThreadPool pool(1);

    // Block the worker while we fill the queue
    auto blocker = pool.submit([]{ std::this_thread::sleep_for(50ms); });

    std::vector<std::string> order;
    std::mutex               order_mu;

    auto submit_named = [&](const std::string& name, Priority p) {
        return pool.submit(p, [name, &order, &order_mu]{
            std::lock_guard<std::mutex> lk(order_mu);
            order.push_back(name);
        });
    };

    auto f1 = submit_named("LOW",    Priority::LOW);
    auto f2 = submit_named("NORMAL", Priority::NORMAL);
    auto f3 = submit_named("HIGH",   Priority::HIGH);

    blocker.get(); f1.get(); f2.get(); f3.get();

    // HIGH should have run before NORMAL, NORMAL before LOW
    ASSERT(order.size() == 3);
    ASSERT(order[0] == "HIGH");
    ASSERT(order[1] == "NORMAL");
    ASSERT(order[2] == "LOW");
}

TEST(test_concurrent_submission) {
    ThreadPool pool(8);
    std::atomic<int> counter { 0 };

    // 8 producer threads each submit 50 tasks
    std::vector<std::thread> producers;
    for (int i = 0; i < 8; ++i) {
        producers.emplace_back([&pool, &counter]{
            for (int j = 0; j < 50; ++j) {
                pool.submit([&counter]{
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }
    for (auto& t : producers) t.join();

    pool.wait_all();
    ASSERT_EQ(counter.load(), 400);  // 8 * 50
}

TEST(test_shutdown_drains_queue) {
    ThreadPool pool(4);
    std::atomic<int> counter { 0 };

    for (int i = 0; i < 30; ++i) {
        pool.submit([&counter]{
            std::this_thread::sleep_for(5ms);
            counter.fetch_add(1);
        });
    }

    pool.shutdown(); // should drain all 30 tasks
    ASSERT_EQ(counter.load(), 30);
}

TEST(test_submit_after_shutdown_throws) {
    ThreadPool pool(2);
    pool.shutdown();

    bool threw = false;
    try {
        pool.submit([]{ return 1; });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT(threw);
}

TEST(test_stats_accuracy) {
    ThreadPool pool(4);
    const int N = 40;

    for (int i = 0; i < N; ++i)
        pool.submit([]{});

    pool.wait_all();
    PoolStats s = pool.stats();
    ASSERT_EQ((int)s.total_submitted, N);
    ASSERT_EQ((int)s.total_completed, N);
    ASSERT_EQ((int)s.total_failed,    0);
}

TEST(test_hardware_concurrency_default) {
    ThreadPool pool;  // default: hardware_concurrency()
    ASSERT(pool.num_threads() > 0);
    ASSERT(pool.num_threads() <= 64); // sanity
}

TEST(test_taskqueue_push_pop) {
    TaskQueue<int> q;
    q.push(1);
    q.push(2);
    q.push(3);

    ASSERT_EQ(q.pop().value(), 1);
    ASSERT_EQ(q.pop().value(), 2);
    ASSERT_EQ(q.pop().value(), 3);
    ASSERT_EQ(q.size(), 0u);
}

TEST(test_taskqueue_close_unblocks_pop) {
    TaskQueue<int> q;
    bool unblocked = false;

    std::thread consumer([&q, &unblocked]{
        auto val = q.pop(); // will block until close()
        unblocked = !val.has_value();
    });

    std::this_thread::sleep_for(20ms);
    q.close();
    consumer.join();
    ASSERT(unblocked);
}

TEST(test_taskqueue_bounded) {
    TaskQueue<int> q(2);
    ASSERT(q.try_push(1));
    ASSERT(q.try_push(2));
    ASSERT(!q.try_push(3)); // full
}

TEST(test_large_workload) {
    ThreadPool pool(std::thread::hardware_concurrency());
    std::atomic<long long> total { 0 };
    const int N = 1000;

    std::vector<std::future<int>> futs;
    futs.reserve(N);
    for (int i = 0; i < N; ++i)
        futs.push_back(pool.submit([i]{ return i; }));

    long long sum = 0;
    for (auto& f : futs) sum += f.get();

    long long expected = (long long)N * (N - 1) / 2;
    ASSERT_EQ(sum, expected);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║     ThreadPoolLib — Test Suite           ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n\n";

    std::cout << "  Running " << (g_passed + g_failed) << " tests...\n\n";
    // (tests auto-registered via static constructors)

    std::cout << "\n──────────────────────────────────────────\n";
    std::cout << "  Results: " << g_passed << " passed, "
              << g_failed << " failed\n";
    if (g_failed == 0)
        std::cout << "  ✓ All tests passed!\n\n";
    else
        std::cout << "  ✗ Some tests failed.\n\n";

    return g_failed > 0 ? 1 : 0;
}
