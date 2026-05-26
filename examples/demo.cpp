/*
 * demo.cpp
 * ========
 * Demonstrates the ThreadPool library with real-world-style tasks:
 *   1. Matrix multiplication (simulating image processing)
 *   2. Gaussian blur convolution
 *   3. Mixed-priority task scheduling
 *   4. Main thread remains responsive throughout
 */

#include "ThreadPool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <cmath>
#include <random>
#include <sstream>

using namespace tpl;
using namespace std::chrono;

// ─── Utility ──────────────────────────────────────────────────────────────────

static void print_header(const std::string& title) {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║  " << std::left << std::setw(40) << title << "║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";
}

static void print_stats(const PoolStats& s) {
    std::cout << "\n  ┌─ Pool Statistics ──────────────────────\n";
    std::cout << "  │  Total Workers  : " << s.total_workers   << "\n";
    std::cout << "  │  Active Workers : " << s.active_workers  << "\n";
    std::cout << "  │  Submitted      : " << s.total_submitted << "\n";
    std::cout << "  │  Completed      : " << s.total_completed << "\n";
    std::cout << "  │  Failed         : " << s.total_failed    << "\n";
    std::cout << "  │  Queue Depth    : " << s.queue_depth     << "\n";
    std::cout << "  │  Avg Wait (ms)  : " << std::fixed
              << std::setprecision(2)   << s.avg_wait_ms      << "\n";
    std::cout << "  └────────────────────────────────────────\n";
}

// ─── Task 1: Matrix Multiplication ───────────────────────────────────────────

using Matrix = std::vector<std::vector<double>>;

Matrix random_matrix(size_t n, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    Matrix m(n, std::vector<double>(n));
    for (auto& row : m)
        for (auto& val : row)
            val = dist(rng);
    return m;
}

Matrix multiply(const Matrix& A, const Matrix& B) {
    size_t n = A.size();
    Matrix C(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i)
        for (size_t k = 0; k < n; ++k)
            for (size_t j = 0; j < n; ++j)
                C[i][j] += A[i][k] * B[k][j];
    return C;
}

double matrix_checksum(const Matrix& M) {
    double s = 0.0;
    for (const auto& row : M)
        for (double v : row)
            s += v;
    return s;
}

// ─── Task 2: Gaussian Blur ────────────────────────────────────────────────────

using Image = std::vector<std::vector<uint8_t>>;

Image random_image(int w, int h, unsigned seed = 99) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    Image img(h, std::vector<uint8_t>(w));
    for (auto& row : img)
        for (auto& p : row)
            p = static_cast<uint8_t>(dist(rng));
    return img;
}

Image gaussian_blur(const Image& src, int radius = 2) {
    int h = static_cast<int>(src.size());
    int w = static_cast<int>(src[0].size());
    Image dst(h, std::vector<uint8_t>(w, 0));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double sum = 0, weight = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    int ny = std::clamp(y + dy, 0, h - 1);
                    int nx = std::clamp(x + dx, 0, w - 1);
                    double sigma = radius / 2.0;
                    double g = std::exp(-(dx*dx + dy*dy) / (2*sigma*sigma));
                    sum    += src[ny][nx] * g;
                    weight += g;
                }
            }
            dst[y][x] = static_cast<uint8_t>(sum / weight);
        }
    }
    return dst;
}

// ─── Demo 1: Basic future-based result retrieval ──────────────────────────────

void demo_basic_futures() {
    print_header("Demo 1: Basic Futures");

    ThreadPool pool(4);

    // Submit arithmetic tasks and collect futures
    std::vector<std::future<long long>> futures;
    for (int i = 1; i <= 10; ++i) {
        futures.push_back(pool.submit([i] {
            // Simulate some work
            long long result = 0;
            for (int j = 1; j <= i * 100'000; ++j) result += j;
            return result;
        }));
    }

    std::cout << "\n  Main thread is free while workers crunch...\n";

    for (int i = 0; i < 10; ++i) {
        long long result = futures[i].get();  // blocks per-result
        std::cout << "  Task " << std::setw(2) << (i+1)
                  << " → sum(1.." << (i+1)*100'000 << ") = "
                  << result << "\n";
    }

    pool.wait_all();
    print_stats(pool.stats());
}

// ─── Demo 2: Matrix multiplication with timing ────────────────────────────────

void demo_matrix_tasks() {
    print_header("Demo 2: Parallel Matrix Multiplication (Image Processing)");

    const size_t N         = 200;  // 200x200 matrix
    const size_t NUM_TASKS = 8;

    ThreadPool pool(std::thread::hardware_concurrency());

    std::cout << "\n  Submitting " << NUM_TASKS
              << " matrix multiplications (" << N << "×" << N << ")...\n";

    auto A = random_matrix(N, 1);
    auto B = random_matrix(N, 2);

    auto t_start = steady_clock::now();

    std::vector<std::future<double>> futs;
    for (size_t t = 0; t < NUM_TASKS; ++t) {
        futs.push_back(pool.submit([&A, &B, t] {
            Matrix C = multiply(A, B);
            return matrix_checksum(C) + static_cast<double>(t); // unique result
        }));
    }

    std::cout << "  Main thread continues running (not blocked)...\n";

    double total = 0;
    for (auto& f : futs) total += f.get();

    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t_start).count();
    std::cout << "  All " << NUM_TASKS << " multiplications done in "
              << elapsed << " ms | checksum=" << std::fixed
              << std::setprecision(1) << total << "\n";

    print_stats(pool.stats());
}

// ─── Demo 3: Priority scheduling ─────────────────────────────────────────────

void demo_priority() {
    print_header("Demo 3: Priority-Based Task Scheduling");

    // Use 1 thread so we can observe ordering clearly
    ThreadPool pool(1);

    std::vector<std::string> execution_order;
    std::mutex order_mutex;

    auto make_task = [&](const std::string& name, Priority p) {
        return pool.submit(p, [name, &execution_order, &order_mutex] {
            std::this_thread::sleep_for(milliseconds(5));
            std::lock_guard<std::mutex> lk(order_mutex);
            execution_order.push_back(name);
        });
    };

    // Saturate the single worker first
    auto blocker = pool.submit([]{ std::this_thread::sleep_for(milliseconds(50)); });

    // Submit tasks with different priorities (queue builds up while worker is busy)
    auto f1 = make_task("LOW-1",    Priority::LOW);
    auto f2 = make_task("NORMAL-1", Priority::NORMAL);
    auto f3 = make_task("HIGH-1",   Priority::HIGH);
    auto f4 = make_task("LOW-2",    Priority::LOW);
    auto f5 = make_task("HIGH-2",   Priority::HIGH);
    auto f6 = make_task("NORMAL-2", Priority::NORMAL);

    blocker.get(); f1.get(); f2.get(); f3.get(); f4.get(); f5.get(); f6.get();

    std::cout << "\n  Execution order (HIGH > NORMAL > LOW expected):\n  ";
    for (const auto& name : execution_order)
        std::cout << "[" << name << "] ";
    std::cout << "\n";
    print_stats(pool.stats());
}

// ─── Demo 4: Gaussian blur pipeline ──────────────────────────────────────────

void demo_blur_pipeline() {
    print_header("Demo 4: Gaussian Blur Pipeline (Simulated Camera Frames)");

    const int W = 640, H = 480, FRAMES = 12;

    ThreadPool pool(4);
    std::cout << "\n  Processing " << FRAMES << " frames ("
              << W << "×" << H << " px) in parallel...\n";

    auto t0 = steady_clock::now();

    std::vector<std::future<size_t>> futs;
    for (int f = 0; f < FRAMES; ++f) {
        futs.push_back(pool.submit([f, W, H] {
            Image frame = random_image(W, H, static_cast<unsigned>(f));
            Image blurred = gaussian_blur(frame, 3);
            // Return sum of pixels as a lightweight "checksum"
            size_t sum = 0;
            for (const auto& row : blurred)
                for (uint8_t p : row)
                    sum += p;
            return sum;
        }));
    }

    for (int f = 0; f < FRAMES; ++f) {
        size_t checksum = futs[f].get();
        std::cout << "  Frame " << std::setw(2) << f
                  << " blurred → pixel sum = " << checksum << "\n";
    }

    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    std::cout << "\n  " << FRAMES << " frames processed in " << elapsed << " ms\n";
    print_stats(pool.stats());
}

// ─── Demo 5: shutdown_now() demonstration ────────────────────────────────────

void demo_shutdown_now() {
    print_header("Demo 5: Immediate Shutdown (shutdown_now)");

    ThreadPool pool(4);

    std::atomic<int> completed { 0 };
    for (int i = 0; i < 50; ++i) {
        pool.submit([&completed] {
            std::this_thread::sleep_for(milliseconds(20));
            completed.fetch_add(1);
        });
    }

    std::this_thread::sleep_for(milliseconds(30)); // let a few tasks run
    std::cout << "\n  Calling shutdown_now() — pending tasks will be dropped.\n";
    pool.shutdown_now();

    std::cout << "  Tasks completed before shutdown: " << completed.load() << " / 50\n";
    std::cout << "  Pool stopped. Queued tasks were discarded.\n";
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║     ThreadPoolLib — Live Demo            ║\n";
    std::cout << "║     Hardware threads: "
              << std::setw(2) << std::thread::hardware_concurrency()
              << "                  ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    demo_basic_futures();
    demo_matrix_tasks();
    demo_priority();
    demo_blur_pipeline();
    demo_shutdown_now();

    std::cout << "\n✓ All demos completed successfully.\n\n";
    return 0;
}
