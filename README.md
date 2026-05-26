# Asynchronous Thread Pool Library (C++)

A lightweight and reusable C++17 thread pool library built from scratch for asynchronous task execution and efficient thread management. The project demonstrates core multithreading concepts such as task scheduling, synchronization, futures, condition variables, and the producer-consumer pattern.

## UI Preview

![Thread Pool Visualization](screenshots/project_structure.png)

## Features

- Fixed-size worker thread pool
- Thread-safe task queue
- Asynchronous task execution using `std::future`
- Priority-based task scheduling
- Graceful and immediate shutdown support
- Runtime statistics collection
- Unit and integration testing
- Interactive HTML visualization
- Modular and reusable design

## Project Structure

```text
ThreadPoolLib/
├── include/
│   ├── ThreadPool.h
│   ├── TaskQueue.h
│   └── WorkerThread.h
├── src/
│   └── ThreadPool.cpp
├── examples/
│   └── demo.cpp
├── tests/
│   └── test_threadpool.cpp
├── screenshots/
│   └── project_structure.png
├── threadpool_ui.html
├── Makefile
└── README.md
```