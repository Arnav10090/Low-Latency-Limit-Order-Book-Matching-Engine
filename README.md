# Order Matching Engine

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![CMake](https://img.shields.io/badge/CMake-3.16-brightgreen)
![Platform: Linux](https://img.shields.io/badge/Platform-Linux-orange)

A C++17 price-time-priority limit order book matching engine built for low-latency systems engineering.

## Overview

This project implements a single-symbol central limit order book with NEW, MODIFY, and CANCEL semantics and a producer-consumer execution model. It demonstrates clear ownership boundaries, lock-free handoff, and reproducible microbenchmarks without external dependencies. Built to focus on core matching logic and measurement methodology.

## Architecture

![Architecture flow](media/architecture_flow.png)

![Memory ownership](media/memory_ownership.png)

Producer threads create `OrderRequest` payloads, run pre-trade risk checks, and push to a lock-free SPSC queue. A single engine thread owns all book state, allocates `Order` objects from a compile-time memory pool, matches orders, and emits trades.

## Features

- Price-time priority matching with FIFO order queues per price level
- Lock-free SPSC queue for producer-engine handoff with cache-line-aligned head/tail
- Compile-time `MemoryPool<Order, 1M>` eliminates per-order heap allocation
- Producer-side pre-trade risk validation before queue insertion
- MODIFY semantics with price-time priority rules (reduce preserves priority, increase resets)
- Thread affinity support via `pthread_setaffinity_np` for benchmark reproducibility
- Serialized TSC latency measurement with calibrated cycle-to-ns conversion

## Benchmark Results

Results from a local run of the included benchmark harness (1,000,000 orders, seed 42).

Distribution: 60% BUY LMT | 20% SELL LMT | 10% CANCEL | 10% MODIFY.

| Percentile | Unpinned (ns) | Pinned Core 2 (ns) |
|---|---|---|
| Mean | 714 | 677 |
| p50 | 500 | 500 |
| p95 | 900 | 900 |
| p99 | 1,200 | 1,300 |
| p99.9 | 16,900 | 25,700 |
| Max | 39,505,800 | 36,444,900 |

**Throughput:** Unpinned 1,358,587 orders/sec | Pinned 1,406,744 orders/sec

**Methodology:** Per-request polling via `lastProcessedRequestId()`. Timestamps: LFENCE/RDTSC start, RDTSCP/LFENCE end. Calibration: median of 7 `steady_clock` windows (250 ms each). Numbers are machine-dependent — reproduce using the included harness.

## Build and Run

### Linux

Prerequisites: `cmake` (>=3.16), GCC or Clang, `make`.

```bash
git clone <repo-url>
cd "Low-Latency Limit Order Book Matching Engine"
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

./matching_engine   # interactive demo
./benchmark         # latency and throughput harness
```

Pinned benchmark run:

```bash
taskset -c 2 ./benchmark
```

### Windows

Prerequisites: `cmake` (>=3.16), Visual Studio or MinGW.

```cmd
git clone <repo-url>
cd "Low-Latency Limit Order Book Matching Engine"
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release

.\Release\matching_engine.exe
.\Release\benchmark.exe > benchmark_run.txt
```

**Note:** Thread affinity and some performance features are optimized for Linux. Windows builds are supported but may show different performance characteristics.

## Design Tradeoffs

`std::map` is used for price levels. It allocates heap nodes per price level and has O(log n) lookup. This was chosen to keep the prototype readable. In a production system with a fixed tick increment, a flat array indexed by `(price - base) / tick_size` gives O(1) level lookup with zero allocation. For a sparse price range, an open-addressing hash map eliminates allocation variance.

The compile-time `MemoryPool<Order>` eliminates per-order heap allocation on the engine hot path. Other allocation sources remain: price-level map nodes and per-match trade output vectors. These are known tradeoffs of this prototype design.

See `docs/` for architecture deep-dive, benchmark methodology, and design decision rationale.
