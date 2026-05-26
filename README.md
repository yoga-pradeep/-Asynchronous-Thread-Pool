# ThreadPoolLib

A production-grade **C++ Asynchronous Thread Pool / Task Queue Library** built from scratch using raw OS-level synchronization primitives.

---

## Project Structure

```
ThreadPoolLib/
├── .vscode/
│   ├── settings.json          ← IntelliSense + C++17 config
│   ├── tasks.json             ← Build & Run tasks (Ctrl+Shift+B)
│   ├── launch.json            ← Debugger config
│   └── c_cpp_properties.json  ← Include paths
│
├── include/
│   ├── ThreadPool.h           ← Main library API (header-only templates)
│   ├── TaskQueue.h            ← Thread-safe blocking queue (standalone)
│   └── WorkerThread.h         ← Worker thread abstraction
│
├── src/
│   └── ThreadPool.cpp         ← Core implementation (mutex, CV, worker loop)
│
├── examples/
│   └── demo.cpp               ← 5 live demos (matrix, blur, priority, etc.)
│
├── tests/
│   └── test_threadpool.cpp    ← 16 unit + integration tests
│
├── Makefile                   ← Build system
└── README.md
```

---

## Quick Start

### Build & Run (VS Code)
Press **`Ctrl+Shift+B`** → select **Build & Run Demo**

### Build & Run (Terminal)
```bash
# Run the demo
make demo

# Run all tests
make tests

# Build as static library
make lib

# Clean
make clean
```

### Manual Compile
```bash
mkdir build
g++ -std=c++17 -Wall -O2 -pthread -Iinclude \
    src/ThreadPool.cpp examples/demo.cpp -o build/demo
./build/demo
```

---

## API Usage

### Basic Submit + Future
```cpp
#include "ThreadPool.h"
using namespace tpl;

ThreadPool pool(4);  // 4 worker threads

// Submit a task — returns std::future<int>
auto future = pool.submit([] {
    return expensive_computation();
});

// Main thread is FREE here — workers are running in background
do_other_work();

// Block only when you need the result
int result = future.get();
```

### With Arguments
```cpp
auto fut = pool.submit([](int x, int y) { return x + y; }, 10, 20);
int result = fut.get();  // 30
```

### Priority Scheduling
```cpp
auto high   = pool.submit(Priority::HIGH,   [] { /* urgent */  });
auto normal = pool.submit(Priority::NORMAL, [] { /* normal  */  });
auto low    = pool.submit(Priority::LOW,    [] { /* deferred */ });
```

### Wait for All Tasks
```cpp
pool.submit(task1);
pool.submit(task2);
pool.wait_all();  // blocks until queue empty + all workers idle
```

### Graceful Shutdown
```cpp
pool.shutdown();      // finishes all queued tasks, then stops workers
// OR
pool.shutdown_now();  // stops immediately, drops pending tasks
```

### Statistics
```cpp
PoolStats s = pool.stats();
std::cout << "Completed: " << s.total_completed << "\n";
std::cout << "Avg wait:  " << s.avg_wait_ms << " ms\n";
```

---

## How It Works

### Core Synchronization Pattern

```
┌──────────────┐   submit()     ┌──────────────────────┐
│  Main Thread │ ─────────────► │   Priority Queue      │
│  (Producer)  │                │  (protected by mutex) │
└──────────────┘                └──────────┬───────────┘
                                           │ queue_cv_.notify_one()
                    ┌──────────────────────▼───────────────────────┐
                    │              Worker Threads (Pool)            │
                    │                                               │
                    │  Thread 0: [sleep] → wake → pop → execute   │
                    │  Thread 1: [sleep] → wake → pop → execute   │
                    │  Thread N: [sleep] → wake → pop → execute   │
                    └───────────────────────────────────────────────┘
```

### Key OS-Level Primitives

| Primitive | Role |
|-----------|------|
| `std::mutex` | Protects `task_queue_` from data races |
| `std::condition_variable queue_cv_` | Workers sleep here; woken when a task is pushed |
| `std::condition_variable idle_cv_` | `wait_all()` sleeps here; woken when `active_count_` → 0 |
| `std::atomic<bool> stop_` | Lock-free signal for immediate shutdown |
| `std::atomic<size_t> active_count_` | Tracks busy workers without holding the lock |
| `std::packaged_task` | Bridges `submit()` → `std::future` for result retrieval |

### Worker Loop (Simplified)
```cpp
void worker_loop() {
    while (true) {
        Task task;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [&]{ return !queue_.empty() || stop_; });
            if (stop_ && queue_.empty()) return;  // exit

            task = pop_highest_priority_task();
            active_count_++;
        }
        // ← Lock released. Parallelism happens here.

        task.func();   // execute (no lock held)
        active_count_--;
        if (active_count_ == 0 && queue_.empty())
            idle_cv_.notify_all();  // wake wait_all()
    }
}
```

---

## Design Decisions

### Why `std::priority_queue` + mutex instead of lock-free?
Lock-free queues (SPSC ring buffers, Michael-Scott queue) are faster for **high-throughput**, low-contention scenarios. A mutex-based queue is:
- Correct by construction (no ABA problem)
- Easier to extend (priority, bounded capacity, statistics)
- Sufficient for task granularity typical in real applications

### Why `std::packaged_task` inside `shared_ptr`?
`std::function<void()>` requires a **copyable** callable. `packaged_task` is move-only. Wrapping in `shared_ptr` makes it copyable (copies the pointer, not the task), enabling storage in `std::function`.

### Why `notify_one()` not `notify_all()` on submit?
`notify_all()` wakes every sleeping thread for one task — a **thundering herd**. Only one wins the mutex; the rest sleep again. `notify_one()` is O(1) and correct.

---

## Requirements

- **Compiler**: GCC 9+ or Clang 10+ with `-std=c++17`
- **OS**: Linux (uses pthreads via `-pthread`)
- **Dependencies**: None (standard library only)

---

## Resume Keywords Covered

`C++17` · `Multithreading` · `std::mutex` · `std::condition_variable` · `std::atomic` · `std::future / std::promise` · `std::packaged_task` · `RAII` · `Memory Management` · `Thread-safe Data Structures` · `Priority Scheduling` · `Linux / pthreads` · `Producer-Consumer Pattern` · `Lock-free Programming` · `Performance Optimization`
