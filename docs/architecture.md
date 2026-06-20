# Architecture

This document explains the system architecture in greater depth than the README. It is intended for interview conversations, code walkthroughs, and to provide the rationale behind implementation choices.

## High-Level Architecture

The system implements a single-process, single-symbol central limit order book with a strict producer-consumer boundary: producers create `OrderRequest` payloads and push them to a lock-free SPSC queue. A single engine thread owns all mutable book state and performs all memory management for `Order` objects.

Key modules:
- Producer (application-side request generation and risk checks)
- Pre-trade risk validator (lightweight, executes on producer)
- SPSC lock-free queue (request handoff)
- Engine loop (single-threaded consumer)
- MemoryPool<Order> (fixed compile-time capacity)
- OrderBook (price-level maps and FIFO queues per level)
- Benchmark harness (serialized-TSC measurements, warmup, metrics)

## Component Breakdown

- Producer Thread
  - Constructs `OrderRequest` representing `NEW`, `MODIFY`, `CANCEL` operations.
  - Runs `PreTradeRisk::validate(req)` for early rejection of invalid requests.
  - Pushes the request into the SPSC queue.

- SPSC Queue
  - Single-producer, single-consumer configuration.
  - Lock-free, using minimal memory-ordering semantics for fast handoff.
  - Stores `OrderRequest` by value (small payloads preferred).

- Engine Thread
  - Repeatedly dequeues `OrderRequest` objects.
  - Allocates/returns `Order` objects from `MemoryPool`.
  - Applies book operations (insert/modify/cancel) and produces `Trade` records.
  - Publishes completion via `completion_signal` or `lastProcessedRequestId()`.

- MemoryPool<Order>
  - Compile-time capacity to avoid dynamic heap allocation in the hot path.
  - Frees returned orders back to a free-list owned by the engine thread.

- OrderBook
  - Price levels represented using `std::map<Price, PriceLevel>` for clarity and deterministic iteration.
  - `PriceLevel` contains a `std::deque<Order*>` to keep FIFO semantics.
  - `std::unordered_map<OrderId, Order*>` may provide O(1) lookup for modify/cancel.

## Data Flow

1. Producer creates `OrderRequest`.
2. Pre-trade risk validators run on the producer.
3. The producer pushes `OrderRequest` into the SPSC queue.
4. Engine dequeues the request, allocates an `Order` if necessary, and updates the `OrderBook`.
5. Matching occurs against the opposite side; trades are recorded into engine-owned buffers.
6. Engine publishes completion via an atomic request id (or writes into a completion table).

## Thread Ownership

- Producer thread owns the creation and preparation of `OrderRequest` objects.
- Engine thread owns `Order` instances, the memory pool, the `OrderBook`, and `Trade` buffers.
- Shared synchronization primitives: the SPSC queue and a lightweight completion token (atomic) for producers to read.

## Memory Ownership

- The `MemoryPool` is owned by the engine and contains raw `Order` storage.
- `Order` pointers are never dereferenced by producers after enqueue; producers only reference request ids.
- Trades are ephemeral engine-owned objects that may be exported by snapshot if required by a consumer.

## Queue Design

- Rationale for SPSC: matching engines often have a single well-defined producer path (e.g., network I/O thread or benchmark driver). SPSC is simpler, faster, and requires weaker memory ordering than MPMC.
- `OrderRequest` minimized in size to reduce cache pressure during enqueue/dequeue.

## Order Book Design

- Price levels use `std::map` for deterministic ordering and clear semantics in interviews.
- `std::deque` is used as the per-price FIFO container because it provides stable references for pointers to `Order` objects.
- `Order` objects are allocated from the engine-owned `MemoryPool`. The current
  implementation locates an order by consulting `order_index_` (which maps an
  `OrderId` to its `Side` and `Price`) and then scanning the per-price
  `std::deque<Order*>` to find the live `Order*`. This approach keeps the
  implementation simple and interview-friendly; for O(1) removal an intrusive
  list or storing iterators in the `Order` record would be required.

## Architecture Diagrams

Visual diagrams are provided in `media/` and referenced from the top-level
README. They include:

- `media/architecture_flow.png` — high-level producer → engine flow
- `media/memory_ownership.png` — memory and ownership model
- `media/benchmark.png` — benchmark output screenshot

Place images in the `media/` folder and reference them from GitHub or your
project page.

## Modify Semantics

- `MODIFY` requests contain either changes which preserve price/time (e.g., quantity) or change-of-price.
- For price changes that affect priority, the engine removes the order from its previous level and reinserts at the new level. Time priority semantics are preserved by placing the modified order at the tail of the new price level if semantics require.

## Risk Validation Flow

- Risk checks are intentionally executed on the producer to reduce the engine's complexity and hot-path branching.
- Checks are deterministic and do not mutate matching state.

## Benchmark Flow

- Warmup stage: run a configurable number of requests without recording final statistics.
- Measurement stage: serialize TSC, submit requests, poll for completion, and record per-request cycles.
- Calibration: treat cycles as the canonical measurement; map TSC ticks to wall-clock units only via a local calibration step that samples multiple `steady_clock` windows and computes an empirical cycles→ns factor (recommend median with outlier rejection). Report raw cycle counts and calibrated nanoseconds with the calibration method and factor.
- Aggregation: compute percentiles and throughput from raw cycle values.

## Shutdown Flow

- The engine supports an orderly shutdown: producer stops pushing new requests, sends a special shutdown request, engine drains the queue, deallocates pool resources, and flushes trade buffers.

## Design Tradeoffs

- Deterministic `std::map` price levels vs. cache-friendly but more complex tree/flat structures: chose `std::map` for clarity and interview readability.
- SPSC design reduces generality (no multiple producers) but improves performance for the intended single-producer benchmark.
- MemoryPool reduces heap activity but complicates memory lifecycle; it is owned by the engine to keep ownership simple.

