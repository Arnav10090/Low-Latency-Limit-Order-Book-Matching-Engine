# Order Matching Engine
A low-latency limit order book and matching engine in C++17, built for HFT-style trading infrastructure.

## Benchmark Results

```
=======================================================
  Dolat Capital — Order Matching Engine Benchmark
=======================================================
  Symbol        : AAPL
  Orders        : 1,000,000
  Distribution  : 60% BUY LMT | 20% SELL LMT | 10% BUY MKT | 5% SELL MKT | 5% CANCEL

  --- Latency (per order) ---
  Min      :          0 ns
  Mean     :        524 ns
  p50      :        300 ns
  p95      :        700 ns
  p99      :      6,200 ns
  p99.9    :     23,800 ns
  Max      : 23,263,400 ns

  --- Throughput ---
  Total time   : 556 ms
  Orders/sec   : 1,798,489

  --- Book State (end of run) ---
  Best Bid     : 100,085 ($1,000.85)
  Best Ask     : (empty)
  Bid levels   : 238
  Ask levels   : 0

  --- Matching Stats ---
  Trades executed : 494,766
  Orders in book  : 355,511
=======================================================
```

## Architecture

```
[Order Submission]
      |
      v
[SPSCQueue] (lock-free ring buffer, 64K slots)
      |
      v
[MatchingEngine.processAll()]
      |
      v
[MemoryPool] ---> [OrderBook]
                       |
               --------|--------
               |               |
           [BidMap]        [AskMap]
       (desc sorted)   (asc sorted)
       std::map<Price, |  std::map<Price,
         PriceLevel,   |    PriceLevel>
       greater<Price>> |
                       |
                       v
                   [Trades]
```

## Key Design Decisions

1. **Integer Prices** — Prices are stored as `int64_t` in cents (e.g., $150.25 → 15025) to avoid floating-point comparison issues. This is standard practice in HFT systems.

2. **Cache-Line-Padded Orders** — The `Order` struct is padded to exactly 64 bytes (one cache line) to prevent false sharing when orders are accessed from the pre-allocated memory pool.

3. **Pool Allocator** — A fixed-size `MemoryPool<Order, 1M>` pre-allocates all order slots at startup, providing O(1) alloc/free with zero `malloc` calls during order processing. This eliminates allocator latency jitter.

4. **Lock-Free SPSC Queue** — A single-producer single-consumer ring buffer using `std::atomic` with `memory_order_acquire`/`release` semantics. No mutexes, no OS scheduler involvement — the queue is wait-free for both push and pop operations.

5. **Price-Time Priority (FIFO)** — Orders at the same price level are matched in FIFO order using `std::deque`. The bid side uses `std::map<Price, PriceLevel, std::greater<Price>>` (descending) so `begin()` is always the best bid; the ask side uses `std::map<Price, PriceLevel>` (ascending) so `begin()` is always the best ask.

6. **Cancel via O(1) Index** — An `unordered_map<OrderId, {Side, Price}>` provides O(1) lookup for cancel requests, followed by a linear scan of the deque at that price level. This is acceptable for typical order book depths.

## Build & Run

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j4
./matching_engine      # interactive demo
./benchmark            # latency benchmark
```

> **Note:** Build with `-j1` if you encounter out-of-memory errors during compilation (the 1M-element memory pool requires significant memory during template instantiation).

## Project Structure

```
order_matching_engine/
├── CMakeLists.txt           # Build configuration (two targets: demo + benchmark)
├── README.md                # This file
├── include/
│   ├── types.h              # Core type aliases (Price, Quantity, OrderId, Side, etc.)
│   ├── order.h              # Order struct (64-byte cache-line aligned)
│   ├── trade.h              # Trade output record
│   ├── memory_pool.h        # Fixed-size O(1) object pool allocator
│   ├── spsc_queue.h         # Lock-free single-producer single-consumer ring buffer
│   ├── order_book.h         # Price-time priority order book with bid/ask maps
│   └── matching_engine.h    # Engine orchestrator (queue → pool → book → trades)
├── src/
│   ├── order_book.cpp       # Matching logic, cancel, book state queries
│   ├── matching_engine.cpp  # Submit/process pipeline implementation
│   └── main.cpp             # Interactive demo (6 scripted order scenarios)
└── benchmark/
    └── bench.cpp            # 1M-order latency benchmark with percentile stats
```
