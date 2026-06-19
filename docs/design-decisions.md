# Design Decisions

This document captures the major engineering decisions, alternatives considered, and the rationale behind choices in this repository.

## Decision: `OrderRequest` vs `Order` on the Queue

- Choice: Queue `OrderRequest` payloads; allocate full `Order` objects on the engine thread.

- Alternatives considered:
  - Enqueue full `Order` objects including engine-owned pointers (would require cross-thread ownership transfer semantics).
  - Enqueue pointers to heap-allocated `Order` objects (introduces cross-thread heap-deallocation and complexity).

- Pros:
  - Keeps engine memory ownership clear: engine exclusively owns `Order` objects.
  - Minimizes producer-side work and memory churn.
  - Simplifies the use of a compile-time memory pool.

- Cons:
  - Extra copy/move of `OrderRequest` payload during enqueue.
  - Slight overhead to allocate `Order` on engine side (amortized by pool).

- Rationale: The clarity of ownership and reduced cross-thread complexity is preferable for interview defensibility and correctness.

## Decision: SPSC vs MPMC Queue

- Choice: SPSC lock-free queue.

- Alternatives:
  - MPMC queue with CAS-based coordination.
  - Lock-based queues with condition variables.

- Pros:
  - Lower latency and weaker memory-ordering requirements.
  - Simpler and less error-prone implementation for single-producer workloads.

- Cons:
  - No native multi-producer scaling without design changes.

- Rationale: The benchmark and the primary intended workloads are single-producer. SPSC gives the best latency characteristics for that use case while keeping memory ordering simpler for correctness.

## Decision: Compile-Time MemoryPool

- Choice: fixed-size `MemoryPool<Order>` with free-list owned by engine.

- Alternatives:
  - Use `new/delete` or `std::allocator` per order.
  - Use an arena allocator with dynamic resizing.

- Pros:
  - Eliminates per-order heap allocation during hot path.
  - Deterministic allocation pattern suitable for microbenchmarks.

- Cons:
  - Fixed capacity requires conservative sizing or risk of exhaustion.
  - Slightly more complex memory management code.

- Rationale: For a portfolio project focused on latency, a compile-time pool highlights memory ownership and reduces allocation jitter that would otherwise complicate measurements.

## Decision: Price-Time Priority

- Choice: Standard exchange semantics: price-time priority.

- Alternatives:
  - Pro-rata matching or size-weighted priority.
  - FIFO-only structures without price bucketing.

- Pros:
  - Recognizable and defensible semantics for interviews and trading roles.
  - Deterministic behavior simplifies correctness proofs and benchmarks.

- Cons:
  - Not optimized for some market microstructure variants like pro-rata.

- Rationale: Price-time priority is the industry standard for many matching engines and the clearest choice for a demonstrative portfolio project.

## Decision: Producer-Side Risk Validation

- Choice: Run pre-trade risk checks on producer thread before enqueue.

- Alternatives:
  - Centralize all risk checks on the engine thread.
  - Push checks to a separate risk microservice (out-of-scope).

- Pros:
  - Keeps the engine hot path simpler and faster.
  - Early rejection reduces wasted queue traffic.

- Cons:
  - Risk checks must be deterministic and consistent across producers.

- Rationale: For single-process microbenchmarks, moving cheap deterministic checks to the producer reduces engine complexity and improves latency.

## Decision: Thread Affinity

- Choice: Optional CPU pinning for producer and engine threads.

- Alternatives:
  - Rely on OS scheduler with no pinning.

- Pros:
  - Lower scheduling jitter and improved cache locality during benchmark runs.

- Cons:
  - Less flexible on multi-tenant machines.

- Rationale: Pinning is a standard practice for microbenchmarks and critical-path servers when measuring worst-case latency.

## Decision: RequestId-Based Completion

- Choice: Use monotonically increasing `RequestId` and `lastProcessedRequestId()` to signal completion.

- Alternatives:
  - Per-request futures or condition variables.
  - Pointers to callback objects that the engine invokes.

- Pros:
  - Minimal cross-thread synchronization.
  - Simple to poll from producer side for latency measurement.

- Cons:
  - Producers must actively poll (or implement wait strategies).

- Rationale: Request-id-based completion is simple, low-overhead, and aligns with the microbenchmark approach.

## Decision: Why Standard Containers Were Kept

- Choice: Use `std::map`, `std::deque`, `std::unordered_map` for clear semantics.

- Alternatives:
  - Develop custom cache-friendly arrays/trees.
  - Use flat maps or specialized data structures for performance.

- Pros:
  - Easier to read and defend in interviews.
  - Reduces implementation complexity while retaining determinism.

- Cons:
  - Not the ultimate in cache-optimized performance.

- Rationale: For portfolio clarity and interviewability, maintainability and correctness are prioritized over maximal micro-optimizations.

## Decision: Why Full Allocation-Free Design Was Rejected

- Choice: Do not pursue a fully allocation-free design for the entire stack.

- Alternatives:
  - Design every container and data structure to be allocation-free.

- Pros of allocation-free:
  - Lowest possible allocation overhead and minimal GC/allocator interference.

- Cons of allocation-free:
  - Much higher implementation complexity and code size.
  - Harder to reason about and defend in short interview windows.

- Rationale: The cost/benefit for a portfolio project favored clarity with targeted allocation reduction (via `MemoryPool`) rather than taking on the complexity of a fully allocation-free system.


