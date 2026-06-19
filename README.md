# Order Matching Engine
A C++17 price-time-priority matching engine built as a focused portfolio project for low-latency trading-system roles.

## Project Positioning

This repository is intended to demonstrate:
- modern C++17
- producer/consumer engine design
- SPSC queue usage with lock-free atomics on the target platform
- cache-aware layout decisions
- exchange-style order-book semantics
- Linux-oriented benchmarking and thread pinning

This repository is not presented as:
- a full exchange
- a production-ready trading platform
- a zero-allocation matching engine

## Architecture

```text
[Producer Thread]
(1) submitRequest(req)
(2) risk_check_.validate(req)
(3) queue_.push(req) -> OrderRequest by value
       |
       v
[SPSCQueue<OrderRequest>] (single producer, single consumer)
       |
       v
[Engine Thread]
(4) Dequeue OrderRequest
(5) Allocate Order from MemoryPool<Order, 1M>
(6) Add / Modify / Cancel in OrderBook
                       |
               --------|--------
               |               |
           [Bid Map]       [Ask Map]
         std::map        std::map
               |               |
               +-------[Trades]
                       |
                       v
(7) completion_signal.store(req.request_id)
```

## Architecture Notes

- `OrderRequest` is the queue payload. Full `Order` objects are created only on the engine thread.
- The `MemoryPool<Order, 1M>` removes per-order heap allocation for order object storage, but the overall engine still uses standard-library containers such as `std::map`, `std::deque`, `std::unordered_map`, and a bounded `std::vector<Trade>` that may allocate during setup or structural growth.
- The order book uses `std::map` price levels and `std::deque` FIFO queues to keep the implementation interview-friendly and easy to reason about.
- Completion signaling is tracked by unique request identity, not by order identity, so `NEW`, `MODIFY`, and `CANCEL` requests can be timed correctly.
- Thread pinning and the benchmark path are designed for Linux-style benchmarking, with local Windows support kept mainly for development and validation.

## What Is Defensible

- Price-time-priority matching for `NEW`, `MODIFY`, and `CANCEL`
- Producer-side pre-trade validation before queue insertion
- Compile-time-capacity memory pool for `Order` storage
- SPSC queue built on lock-free atomics on supported target architectures
- Per-request completion polling via `lastProcessedRequestId()`
- Linux affinity path using `pthread_setaffinity_np()`

## What I Do Not Claim

- No claim that the entire hot path is allocation-free
- No claim that the code is production-ready as an exchange core
- No claim that benchmark numbers are universal across machines
- No claim that the benchmark alone proves real-world exchange latency

## Benchmark

Run `./benchmark` on Linux to capture the current pinned vs unpinned numbers for your machine.

Current methodology:
- warmup with 10,000 requests
- pre-seed a resting book and generate deterministic crossing flow
- submit one request at a time and poll `lastProcessedRequestId()`
- measure with `LFENCE/RDTSC` at start and `RDTSCP/LFENCE` at end on x86
- calibrate TSC against multiple `steady_clock` windows
- report mean, p50, p95, p99, p99.9, max, and throughput

Benchmark notes:
- Results are machine-dependent and should be treated as local measurements, not portable headline numbers.
- The Linux benchmark path is the target environment for affinity and latency discussion.
- Windows builds are useful for development validation, but Linux is the intended benchmark platform.

## Build & Run

Linux:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j4
./matching_engine
./benchmark
```

Windows development build:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\matching_engine.exe
.\build\Release\benchmark.exe
```

> Note: the benchmark discussion in this repository is Linux-first even though the codebase can also be built locally on Windows.

## Interview-Ready Summary

This is best presented as:
- a focused low-latency matching-engine portfolio project
- a demonstration of systems thinking and trading-domain literacy
- an implementation that prioritizes correctness, ownership boundaries, and benchmark transparency over feature breadth

For interviews, the most defensible description is:

> Built a C++17 price-time-priority order matching engine with producer-side risk checks, an SPSC request queue, compile-time-capacity order storage, and a Linux-oriented benchmark harness using per-request completion polling and serialized TSC timing.

## Project Structure

```text
order_matching_engine/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── types.h
│   ├── order.h
│   ├── trade.h
│   ├── memory_pool.h
│   ├── spsc_queue.h
│   ├── pre_trade_risk.h
│   ├── order_book.h
│   └── engine_thread.h
├── src/
│   ├── order_book.cpp
│   ├── engine_thread.cpp
│   └── main.cpp
└── benchmark/
    └── bench.cpp
```
