# Project Summary (Recruiter / Hiring Manager)

One-page summary (suitable for a recruiter or hiring manager):

This repository implements a focused C++17 low-latency limit order book matching engine designed as a portfolio project to demonstrate systems engineering skills for quant trading roles.

Problem solved
- Built a deterministic single-process central limit order book (CLOB) with `NEW`, `MODIFY`, and `CANCEL` semantics to demonstrate the core matching logic required by trading systems.

Technical challenges
- Minimized handoff latency between producer and engine using a lock-free SPSC queue.
- Controlled memory allocation on the hot path using a compile-time `MemoryPool<Order>` to reduce allocation jitter.
- Measured per-request latency using serialized TSC cycle counts as the high-resolution measurement; nanosecond values are produced by a local calibration step and reported as calibrated estimates alongside raw cycle counts.

Technologies used
- C++17, `std::map`/`std::deque`/`std::unordered_map` for clear and deterministic data structures.
- Lock-free SPSC queue with atomic memory ordering for handoff.
- Compile-time memory pool pattern for deterministic allocation behavior.
- Linux-first benchmark harness using serialized TSC cycle counts; conversion to nanoseconds requires local calibration and is presented as an estimate.

Why this is relevant to low-latency trading
- Demonstrates clear ownership boundaries, a low-latency handoff path, deterministic matching semantics, and practical benchmarking techniques — all critical skills for engineering roles in electronic market-making and exchange systems.

Contact / Next steps
- See the top-level README for build/run instructions and the `docs/` folder for architecture and benchmarking deep dives.

