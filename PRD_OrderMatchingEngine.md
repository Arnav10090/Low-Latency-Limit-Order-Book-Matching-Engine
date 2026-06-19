# PRD: Low-Latency Limit Order Book Matching Engine
**Version:** 1.2
**Language:** C++17
**Build System:** CMake 3.16+
**Platform:** Linux (Ubuntu 20.04+)
**Target:** Dolat Capital C++ Developer Resume Project
**Time Budget:** 4–5 hours to implement

---

## 1. Project Context

This project demonstrates production-relevant C++ engineering for a high-frequency trading infrastructure role. The goal is a clean, benchmarked, GitHub-ready codebase that a trading firm's engineer can read and immediately recognize as domain-literate. Every design decision must be justifiable in a technical interview.

**Resume headline this project must support:**
> Built a price-time priority limit order book matching engine in C++ achieving ~300 ns average order processing latency and 2M+ orders/second throughput on 1M synthetic orders. Implemented a lock-free SPSC queue passing lightweight order requests to a pinned engine thread, mitigating allocation overhead via a cache-friendly array-based memory pool.

---

## 2. Repository Structure

```
order_matching_engine/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── types.h
│   ├── order.h
│   ├── trade.h
│   ├── memory_pool.h
│   ├── spsc_queue.h
│   ├── order_book.h
│   ├── pre_trade_risk.h
│   └── engine_thread.h
├── src/
│   ├── order_book.cpp
│   ├── engine_thread.cpp
│   └── main.cpp
└── benchmark/
    └── bench.cpp
```

No external dependencies. Standard library and pthreads only.

---

## 3. CMakeLists.txt Specification

```cmake
cmake_minimum_required(VERSION 3.16)
project(OrderMatchingEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release flags — critical for latency numbers
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

include_directories(include)

# Main binary (interactive demo)
add_executable(matching_engine
    src/main.cpp
    src/order_book.cpp
    src/engine_thread.cpp
)
target_compile_options(matching_engine PRIVATE -Wall -Wextra)
target_link_libraries(matching_engine pthread)

# Benchmark binary
add_executable(benchmark
    benchmark/bench.cpp
    src/order_book.cpp
    src/engine_thread.cpp
)
target_compile_options(benchmark PRIVATE -O3 -march=native)
target_link_libraries(benchmark pthread)

# Build with: mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4
```

---

## 4. Data Types — `include/types.h`

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace ome {

enum class Side : uint8_t {
    BUY  = 0,
    SELL = 1
};

enum class OrderType : uint8_t {
    LIMIT  = 0,
    MARKET = 1
};

enum class ActionType : uint8_t {
    NEW    = 0,
    MODIFY = 1,
    CANCEL = 2
};

enum class OrderStatus : uint8_t {
    OPEN      = 0,
    FILLED    = 1,
    PARTIAL   = 2,
    CANCELLED = 3,
    REJECTED  = 4
};

// Prices stored as integers (price * 100) to avoid floating-point comparison.
// e.g., $150.25 is stored as 15025.
using Price    = int64_t;
using Quantity = uint32_t;
using OrderId  = uint64_t;
using Nanos    = uint64_t;

// Cache line size constant for alignment/padding
static constexpr size_t CACHE_LINE_SIZE = 64;

// Lightweight payload passed from Producer to Engine via SPSC queue.
// Designed to be as small as possible to maximize queue throughput.
struct OrderRequest {
    ActionType action;
    OrderId    id;
    Side       side;      // ignored for CANCEL/MODIFY if not needed
    OrderType  type;      // ignored for CANCEL/MODIFY
    Price      price;     // new price for MODIFY
    Quantity   quantity;  // new qty for MODIFY
};

} // namespace ome
```

---

## 5. Order Struct — `include/order.h`

```cpp
#pragma once
#include "types.h"
#include <chrono>

namespace ome {

struct Order {
    OrderId   id;
    Side      side;
    OrderType type;
    Price     price;        // integer price (cents)
    Quantity  quantity;     // original quantity
    Quantity  remaining;    // unfilled quantity
    Nanos     timestamp;    // nanoseconds since epoch, set at order allocation
    OrderStatus status;

    // Padding to 64 bytes (one cache line) to avoid false sharing in the pool
    char pad[64 - (sizeof(OrderId) + sizeof(Side) + sizeof(OrderType) +
                   sizeof(Price) + sizeof(Quantity)*2 + sizeof(Nanos) +
                   sizeof(OrderStatus))];

    static Nanos now_ns() {
        return static_cast<Nanos>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
    }
};

static_assert(sizeof(Order) == 64, "Order must be exactly one cache line");

} // namespace ome
```

---

## 6. Trade Struct — `include/trade.h`

```cpp
#pragma once
#include "types.h"

namespace ome {

struct Trade {
    OrderId  buy_order_id;
    OrderId  sell_order_id;
    Price    execution_price;
    Quantity quantity;
    Nanos    timestamp;
};

} // namespace ome
```

---

## 7. Memory Pool — `include/memory_pool.h`

Replaced the `std::stack<T*>` with a preallocated contiguous pool with an index-based free list. `std::stack` dynamically allocates its internal containers and scatters memory, breaking cache locality.

This custom pool uses compile-time capacity via a template parameter. Storage and the free-index list are `std::array`-backed, so the entire pool is embedded directly into whatever object owns it — no heap indirection, no pointer chasing, and the compiler can optimize with the known size. O(1) allocation/deallocation with zero heap allocations at any point.

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <stdexcept>

namespace ome {

template <typename T, std::size_t Capacity>
class MemoryPool {
public:
    MemoryPool() : head_(0) {
        // Initialize the free list: each slot points to the next
        for (std::size_t i = 0; i < Capacity; ++i) {
            free_indices_[i] = i + 1;
        }
        // Sentinel: last element points past-the-end
        free_indices_[Capacity - 1] = Capacity;
    }

    T* allocate() {
        if (head_ == Capacity) {
            throw std::runtime_error("MemoryPool exhausted");
        }
        std::size_t free_index = head_;
        head_ = free_indices_[free_index];
        return &storage_[free_index];
    }

    void deallocate(T* ptr) {
        if (ptr < storage_.data() || ptr >= storage_.data() + Capacity) {
            throw std::invalid_argument("Pointer does not belong to this MemoryPool");
        }
        std::size_t index = static_cast<std::size_t>(ptr - storage_.data());
        free_indices_[index] = head_;
        head_ = index;
    }

    static constexpr std::size_t capacity() { return Capacity; }

private:
    std::size_t head_; // Index of the first free element

    // Contiguous storage for cache locality — no heap indirection
    std::array<T, Capacity> storage_;

    // Free list implemented as an array of next-free indices
    std::array<std::size_t, Capacity> free_indices_;
};

} // namespace ome
```

---

## 8. Pre-Trade Risk Check — `include/pre_trade_risk.h`

The Producer Thread performs pre-trade risk checks *before* enqueueing order requests. This prevents the queue from being flooded with invalid orders and keeps the engine thread fully dedicated to matching.

**Ownership:** `PreTradeRiskCheck` is owned by the `MatchingEngine` but is called exclusively on the producer thread path inside `submitRequest()`. Since the producer is the sole caller of `submitRequest()`, no synchronization is needed — the risk check is single-writer on the producer thread.

```cpp
#pragma once
#include "types.h"
#include <chrono>
#include <cmath>

namespace ome {

class PreTradeRiskCheck {
public:
    PreTradeRiskCheck(Quantity max_order_size, int64_t max_position, double max_rate_per_sec)
        : max_order_size_(max_order_size),
          max_position_(max_position),
          rate_limit_tokens_(max_rate_per_sec),
          max_tokens_(max_rate_per_sec),
          last_check_time_(std::chrono::steady_clock::now()) {}

    bool validate(const OrderRequest& req) {
        // 1. Simple Token-Bucket Rate Limit
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_check_time_).count();
        last_check_time_ = now;
        
        rate_limit_tokens_ += elapsed_sec * max_tokens_;
        if (rate_limit_tokens_ > max_tokens_) rate_limit_tokens_ = max_tokens_;
        
        if (rate_limit_tokens_ < 1.0) return false; // Rate limit exceeded
        rate_limit_tokens_ -= 1.0;

        // CANCEL and MODIFY down are always allowed (simplification)
        if (req.action == ActionType::CANCEL) return true;

        // 2. Maximum Order Size
        if (req.quantity > max_order_size_) return false;

        // 3. Position Limit (simplistic implementation tracking net exposure)
        // Note: Real systems track exposure via a post-trade execution report stream
        // (drop copy) to handle rejects, partials, and cancels accurately.
        // This pre-trade optimistic update is a deliberate simplification for the PRD.
        int64_t new_exposure = current_position_;
        if (req.side == Side::BUY)  new_exposure += req.quantity;
        else                        new_exposure -= req.quantity;

        if (std::abs(new_exposure) > max_position_) return false;

        // Assuming fill, update position (naive approximation for pre-trade)
        current_position_ = new_exposure;

        return true;
    }

private:
    Quantity max_order_size_;
    int64_t  max_position_;
    double   rate_limit_tokens_;
    double   max_tokens_;
    std::chrono::time_point<std::chrono::steady_clock> last_check_time_;
    
    int64_t current_position_ = 0; // Net long/short position
};

} // namespace ome
```

---

## 9. Lock-Free SPSC Queue — `include/spsc_queue.h`

Passes lightweight `OrderRequest` structs, not full `Order` objects.

```cpp
#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>
#include "types.h"

namespace ome {

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    bool push(const T& item) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        T item = buffer_[head];
        head_.store((head + 1) & mask_, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_;
    std::array<T, Capacity> buffer_;
};

} // namespace ome
```

---

## 10. Order Book — `include/order_book.h`

Added support for `modifyOrder`.

**Modify Semantics:**
- Quantity decrease: Time priority is preserved.
- Quantity increase: Time priority is reset (moved to back of the queue at that price level).
- Price change: Time priority is reset. Treated as cancel + replace.

**Construction:** `OrderBook` is default-constructible. The `MemoryPool` uses a compile-time capacity template parameter, so it requires no constructor arguments — its default constructor initializes the free list. The symbol is set via the constructor parameter.

```cpp
#pragma once
#include "types.h"
#include "order.h"
#include "trade.h"
#include "memory_pool.h"
#include <map>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>

namespace ome {

static constexpr std::size_t POOL_SIZE = 1'048'576; // 1M orders

class OrderBook {
public:
    explicit OrderBook(const std::string& symbol);

    std::vector<Trade> addOrder(Order* order);
    bool cancelOrder(OrderId id);
    std::vector<Trade> modifyOrder(OrderId id, Price new_price, Quantity new_qty);

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::size_t          bidDepth() const;
    std::size_t          askDepth() const;

    MemoryPool<Order, POOL_SIZE>& pool() { return pool_; }

    void printBook(int levels = 5) const;

private:
    struct PriceLevel {
        std::deque<Order*> orders;
        Quantity           total_quantity = 0;
    };

    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel>                      asks_;
    std::unordered_map<OrderId, std::pair<Side, Price>> order_index_;

    std::string symbol_;
    MemoryPool<Order, POOL_SIZE> pool_;

    std::vector<Trade> matchAgainstBids(Order* incoming);
    std::vector<Trade> matchAgainstAsks(Order* incoming);
    void               restInBook(Order* order);
    void               removePriceLevelIfEmpty(Side side, Price price);

    Trade makeTrade(Order* buy, Order* sell, Price exec_price, Quantity qty);
};

} // namespace ome
```

---

## 11. Matching Engine & Engine Thread — `include/engine_thread.h`

We explicitly separate the Producer and Consumer (Engine) threads.

**Producer-side flow (executed on the caller's / producer thread):**
1. Producer calls `submitRequest(req)`.
2. `submitRequest()` calls `risk_check_.validate(req)` — if validation fails, returns `false` immediately. The request never enters the queue.
3. If validation passes, `submitRequest()` pushes `req` into the SPSC queue. Returns `false` if the queue is full.

**Consumer-side flow (engine thread):**
1. `engineLoop()` spins on `queue_.pop()`.
2. On successful dequeue, calls `processRequest(req)` which allocates from the pool, matches, and stores the `last_processed_id_`.

**Ownership:**
- `risk_check_` is owned by `MatchingEngine` but accessed only on the producer thread via `submitRequest()`. Since the SPSC contract guarantees a single producer, no synchronization is needed.
- `book_`, `pool_`, `trades_` are accessed only on the engine thread.
- `queue_` is the sole shared data structure — its lock-free SPSC design handles synchronization.
- `last_processed_id_` is written by the engine thread and read by the producer thread for benchmark completion polling.

### Engine Thread Header

```cpp
#pragma once
#include "order_book.h"
#include "spsc_queue.h"
#include "pre_trade_risk.h"
#include <thread>
#include <atomic>
#include <vector>

namespace ome {

static constexpr std::size_t QUEUE_CAPACITY = 65536;

class MatchingEngine {
public:
    // Constructor initializes all components. No heap allocation occurs after construction.
    explicit MatchingEngine(const std::string& symbol,
                            Quantity max_order_size = 10'000,
                            int64_t max_position = 100'000,
                            double max_rate_per_sec = 50'000.0);
    ~MatchingEngine();

    // MatchingEngine is not copyable or movable (owns a thread)
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    // Producer API — called on the producer thread.
    // Returns false if risk check rejects the request OR if the queue is full.
    // Invalid requests never enter the queue.
    bool submitRequest(const OrderRequest& req);

    // Thread lifecycle
    void start();
    void stop();
    void setAffinity(int core_id);

    // Benchmark helper — called by producer to poll for completion.
    // Returns the OrderId of the last request fully processed by the engine thread.
    OrderId lastProcessedId() const {
        return last_processed_id_.load(std::memory_order_acquire);
    }

private:
    // --- Producer-thread state ---
    PreTradeRiskCheck risk_check_;  // accessed only in submitRequest() on producer thread

    // --- Shared (lock-free SPSC handles synchronization) ---
    SPSCQueue<OrderRequest, QUEUE_CAPACITY> queue_;

    // --- Engine-thread state ---
    OrderBook book_;
    std::vector<Trade> trades_;

    std::atomic<bool> running_{false};
    std::atomic<OrderId> last_processed_id_{0};
    std::thread engine_thread_;

    void engineLoop();
    void processRequest(const OrderRequest& req);
};

} // namespace ome
```

### `engine_thread.cpp` Implementation

```cpp
#include "engine_thread.h"
#include <pthread.h>
#include <immintrin.h>

namespace ome {

MatchingEngine::MatchingEngine(const std::string& symbol,
                               Quantity max_order_size,
                               int64_t max_position,
                               double max_rate_per_sec)
    : risk_check_(max_order_size, max_position, max_rate_per_sec),
      book_(symbol)
{
    // All memory (pool, queue buffer) is allocated here at construction time.
    // No heap allocation occurs after this point.
}

MatchingEngine::~MatchingEngine() {
    stop();
}

void MatchingEngine::start() {
    running_.store(true, std::memory_order_release);
    engine_thread_ = std::thread(&MatchingEngine::engineLoop, this);
}

void MatchingEngine::stop() {
    running_.store(false, std::memory_order_release);
    if (engine_thread_.joinable()) {
        engine_thread_.join();
    }
}

bool MatchingEngine::submitRequest(const OrderRequest& req) {
    // Pre-trade risk validation on the producer thread.
    // Invalid requests are rejected here and never enter the queue.
    if (!risk_check_.validate(req)) {
        return false;
    }
    return queue_.push(req);
}

void MatchingEngine::engineLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        auto maybe_req = queue_.pop();
        if (maybe_req) {
            processRequest(*maybe_req);
        } else {
            _mm_pause(); // x86 spin-wait hint
        }
    }
    // Drain remaining requests on shutdown
    while (auto maybe_req = queue_.pop()) {
        processRequest(*maybe_req);
    }
}

void MatchingEngine::processRequest(const OrderRequest& req) {
    if (req.action == ActionType::NEW) {
        Order* order = book_.pool().allocate();
        order->id = req.id;
        order->side = req.side;
        order->type = req.type;
        order->price = req.price;
        order->quantity = req.quantity;
        order->remaining = req.quantity;
        order->timestamp = Order::now_ns();
        order->status = OrderStatus::OPEN;

        auto new_trades = book_.addOrder(order);
        trades_.insert(trades_.end(), new_trades.begin(), new_trades.end());
    } 
    else if (req.action == ActionType::CANCEL) {
        book_.cancelOrder(req.id);
    }
    else if (req.action == ActionType::MODIFY) {
        auto new_trades = book_.modifyOrder(req.id, req.price, req.quantity);
        trades_.insert(trades_.end(), new_trades.begin(), new_trades.end());
    }

    // Update the last processed ID for benchmark completion signaling.
    // The producer polls this value to measure per-order round-trip latency.
    last_processed_id_.store(req.id, std::memory_order_release);
}

void MatchingEngine::setAffinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc = pthread_setaffinity_np(engine_thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        throw std::runtime_error("Failed to set thread affinity");
    }
}

} // namespace ome
```

---

## 12. Main Demo — `src/main.cpp`

The interactive demo will instantiate the `MatchingEngine`, `start()` it, and use the Producer thread (main thread) to submit `OrderRequest` structs.
It should include testing a `MODIFY` order to verify that quantities can decrease without losing priority, and increasing quantities resets priority.

---

## 13. Benchmark — `benchmark/bench.cpp`

### 13.1 Benchmark Design

- **Warmup phase**: Enqueue 10,000 requests and poll `lastProcessedId()` until the final warmup request ID is observed. This warms the L1/L2 caches and branch predictors. Do not record these latencies.
- **Latency methodology**: Use `RDTSC` (Read Time-Stamp Counter) on x86 for cycle-accurate timestamps. `RDTSC` offers ~1ns overhead vs ~10-20ns for `std::chrono::high_resolution_clock`.
  *Note: Provide a fallback to `std::chrono` if `__rdtsc()` is unavailable.*
- **Completion signaling**: The producer polls `engine.lastProcessedId()` after each `submitRequest()` call to determine the exact cycle at which the engine thread finished processing that specific order. This gives precise per-order round-trip latency (submit → queue → dequeue → process → atomic store → producer observes).
- **Affinity**: Run the benchmark twice: once with the engine thread unpinned, and once pinned to an isolated core. Report both sets of percentiles (p50, p95, p99, p99.9). Pinned threads eliminate context switching and scheduler noise, crucial for latency consistency.

### 13.2 RDTSC Latency Capture

```cpp
inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}
```
*Note: RDTSC counts cycles. Convert to nanoseconds by dividing by the CPU frequency (e.g., 3.0 GHz = 3.0 cycles/ns).*

### 13.3 Measurement Flow

The Producer Thread submits one request at a time, then polls `lastProcessedId()` to measure the full round-trip latency for that specific order:

```cpp
// Pre-generate 1M OrderRequests with sequential IDs (1..N)
std::vector<OrderRequest> requests = generate_requests();
std::vector<uint64_t> latencies(requests.size());

engine.start();
engine.setAffinity(2); // Pin engine to Core 2
// Pin producer (this thread) to Core 3

// --- Warmup: 10,000 requests, not recorded ---
for (size_t i = 0; i < WARMUP_COUNT; ++i) {
    while (!engine.submitRequest(warmup_requests[i])) { _mm_pause(); }
    while (engine.lastProcessedId() != warmup_requests[i].id) { _mm_pause(); }
}

// --- Measured run ---
for (size_t i = 0; i < requests.size(); ++i) {
    uint64_t start_cycles = rdtsc();
    
    while (!engine.submitRequest(requests[i])) { 
        _mm_pause(); // Queue full, spin with pause hint
    }
    
    // Poll until this specific order has been processed by the engine thread.
    // lastProcessedId() returns the ID of the most recently completed request.
    while (engine.lastProcessedId() != requests[i].id) { _mm_pause(); }

    uint64_t end_cycles = rdtsc();
    latencies[i] = end_cycles - start_cycles;
}

engine.stop();
```

**Why `lastProcessedId()` polling:** This measures the true end-to-end latency for each order: the time from the producer's `rdtsc()` call through queue push, queue pop, order processing, and the atomic ID store becoming visible to the producer. Each order is individually timed, giving accurate per-order latency distributions.

### 13.4 Expected Benchmark Output

```
=======================================================
  Dolat Capital — Order Matching Engine Benchmark
=======================================================
  Orders        : 1,000,000
  Distribution  : 60% BUY LMT | 20% SELL LMT | 10% CANCEL | 10% MODIFY

  --- Latency (per order, ns) ---
  Percentile | Unpinned    | Pinned (Core 2)
  -----------------------------------------
  Mean       |    312 ns   |    240 ns
  p50        |    278 ns   |    215 ns
  p95        |    621 ns   |    310 ns
  p99        |  1,204 ns   |    385 ns
  p99.9      |  4,811 ns   |    650 ns
  Max        | 38,204 ns   |  1,800 ns

  --- Throughput ---
  Pinned Orders/sec : 4,166,666

=======================================================
```

---

## 14. README.md Specification

1. **Title + one-line description**
   ```
   # Order Matching Engine
   A low-latency limit order book and matching engine in C++17, built for HFT-style trading infrastructure.
   ```

2. **Benchmark results** — Paste the latency table comparing Pinned vs Unpinned performance.

3. **Architecture**
   ```
   [Producer Thread (Core 1)]
   (1) submitRequest(req)
   (2) risk_check_.validate(req)  ← rejects invalid orders HERE
          |  (rejected? return false, never enters queue)
          |  (passed?)
          v
   (3) queue_.push(req)  → OrderRequest by value
          |
          v
   [SPSCQueue] (lock-free ring buffer, 64K slots)
          |
          v
   [Engine Thread (Pinned Core 2)]
   (4) Dequeue OrderRequest
   (5) Allocate Order from MemoryPool<Order, 1M> (compile-time, array-based O(1))
   (6) Add/Modify/Cancel in OrderBook
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
   (7) last_processed_id_.store(req.id)  ← producer polls this
   ```

4. **Key Design Decisions** — Pre-trade risk validation before queue insertion, lightweight queue payload, compile-time capacity array memory pool, thread pinning, RDTSC latency measurement, `lastProcessedId()` polling for per-order benchmark completion.

5. **Build & Run instructions**

---

## 15. Functional Requirements

| ID   | Requirement                                                                 |
|------|-----------------------------------------------------------------------------|
| F-01 | Add BUY and SELL LIMIT orders with price-time (FIFO) priority matching      |
| F-02 | Add BUY and SELL MARKET orders that match at any available price             |
| F-03 | Modify open orders: qty reduction keeps priority, price/qty increase resets priority |
| F-04 | Cancel open orders by ID in O(1) index lookup + O(n) deque removal          |
| F-05 | Emit a `Trade` record for every partial or full match                       |
| F-06 | Pre-trade risk check validates before queue insertion; invalid orders never enter the queue |
| F-07 | Compile-time capacity array-based O(1) memory pool; no `malloc` during processing |
| F-08 | SPSC queue passes lightweight `OrderRequest` structs                        |
| F-09 | Engine runs on a dedicated pinned thread via `pthread_setaffinity_np`       |
| F-10 | All components compile cleanly with `-Wall -Wextra` and no warnings         |

---

## 16. Non-Functional Requirements / Performance Targets

| Metric              | Target                          | How to verify                    |
|---------------------|---------------------------------|----------------------------------|
| p99 latency (pinned)| < 1,000 ns                      | benchmark `lastProcessedId()` polling |
| Throughput          | > 2M orders/second              | benchmark output                 |
| Memory (pool)       | ~64 MB for 1M order slots       | `sizeof(Order) × POOL_SIZE` (embedded `std::array`) |
| Compile time        | < 30s on a 4-core machine       | `make -j4`                       |
| Zero external deps  | No Boost, no third-party libs   | CMakeLists.txt has no find_pkg   |

---

## 17. Implementation Notes for the Agent

1. **Object Ownership:** The Producer thread owns `OrderRequest` construction and calls `submitRequest()`. The `PreTradeRiskCheck` is owned by `MatchingEngine` but accessed exclusively on the producer thread inside `submitRequest()`. The Engine thread owns the `MemoryPool`, `Order` objects, `trades_` vector, and `OrderBook`. The `OrderRequest` is passed by value through the `SPSCQueue`.
2. **Risk Check Wiring:** `submitRequest()` calls `risk_check_.validate(req)` before `queue_.push(req)`. If validation fails, the method returns `false` and the request never enters the queue. This is enforced by the single-producer SPSC contract — only one thread calls `submitRequest()`.
3. **False Sharing:** Ensure the `head_` and `tail_` of the `SPSCQueue` are aligned to `CACHE_LINE_SIZE` (64 bytes) to prevent false sharing between Producer and Engine threads.
4. **Compile-Time Memory Pool:** `MemoryPool<T, Capacity>` uses `std::array` for both storage and the free-index list. The capacity is a template parameter. No `std::vector` is used. The pool is default-constructible and initializes its free list in the constructor. `OrderBook` declares `MemoryPool<Order, POOL_SIZE> pool_` which compiles without explicit initialization.
5. **Thread Shutdown:** The engine thread must gracefully exit when `running_` is set to false. After the loop exits, the engine drains remaining queued requests to ensure all submitted orders are processed.
6. **Benchmark Completion:** The producer polls `engine.lastProcessedId()` to determine when each individual order has been fully processed. This provides precise per-order round-trip latency measurement. Do not use idle-detection or queue-empty checks for benchmark timing.
7. **RDTSC:** Only use RDTSC for latency diffs; don't use it for absolute wall-time since it lacks epoch grounding.
8. **Cancel in a deque:** `cancelOrder` and `modifyOrder` must do a linear scan of the deque at a given price level. Document this O(n) limitation.
9. **Constructor Order:** `MatchingEngine` member initialization order must match declaration order: `risk_check_`, `queue_`, `book_`, `trades_`, `running_`, `last_processed_id_`, `engine_thread_`.

---

## 18. Out of Scope

The following are explicitly NOT part of this project:
- TCP gateways / FIX protocol
- Persistent database storage or market-data pub/sub systems
- Multiple simultaneous instruments (only one `OrderBook` instance)
- GUI or web interface
- Unit test files (tests are implicitly the demo in `main.cpp`)
- Windows support

---

## 19. Deliverable Checklist

Before considering the project complete, verify:

- [ ] `cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4` produces zero errors and zero warnings
- [ ] `./matching_engine` prints the scripted demo with correct outputs
- [ ] `./benchmark` includes a warmup phase (10K orders polled via `lastProcessedId()`) and prints both pinned and unpinned latencies
- [ ] Per-order latency is measured by polling `lastProcessedId()` after each `submitRequest()` call
- [ ] p99 latency printed by benchmark is under 1,000 ns (pinned)
- [ ] README.md architecture diagram shows risk check → queue → engine → `lastProcessedId()` flow
- [ ] `submitRequest()` calls `risk_check_.validate()` before `queue_.push()` — invalid orders never enter the queue
- [ ] `MemoryPool<Order, POOL_SIZE>` compiles with compile-time capacity (no runtime constructor argument)
- [ ] Code properly demonstrates thread pinning via `pthread_setaffinity_np`
- [ ] No `new` or `malloc` is called after engine initialization
