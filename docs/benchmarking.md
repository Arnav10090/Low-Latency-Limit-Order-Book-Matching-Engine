# Benchmarking Methodology

This document defines the strategy used to measure and defend microbenchmark numbers for the matching engine. It is intended to help you explain and justify the methodology during interviews.

## Goals

- Produce reproducible microbenchmark numbers for local machine comparisons.
- Measure per-request latency and aggregate throughput under deterministic conditions.
- Provide defensible measurement techniques for interview discussion (serialized TSC, calibration, percentiles).

## Measurement Strategy

1. Warmup: run a configurable warmup (default: 10k requests) to stabilize CPU frequency governors, caches, and branch predictors.
2. Calibration: treat TSC cycle deltas as the canonical high-resolution measurement and convert cycles → wall time only via an explicit local calibration step. The calibration procedure measures cycles per nanosecond across multiple `steady_clock` windows and produces an empirical factor (e.g., median cycles/ns) which is used to present times in nanoseconds as calibrated estimates. Always publish the calibration factor and method alongside any converted wall-time numbers.
3. Serialization: use `LFENCE; RDTSC` at request start and `RDTSCP; LFENCE` at request end on x86 to ensure accurate cycle counts across out-of-order execution.
4. Submission/Completion: submit one request and poll `lastProcessedRequestId()` (completion token) until the engine has processed that request. Record serialized TSC delta.
5. Aggregation: compute mean, p50, p95, p99, p99.9, max, and throughput (requests/sec).

### Why serialized TSC

- RDTSC alone is affected by instruction reordering and speculative execution. Using LFENCE and RDTSCP ensures the read is ordered with respect to the measured operations.
- Cycle-level resolution is useful for microbenchmarks where wall-clock timers lack sufficient granularity.
- Best practice: treat cycle counts as the primary result. Convert cycles to nanoseconds with the calibration factor for readability, but report the cycles alongside calibrated nanoseconds and document the calibration assumptions (CPU model, governor/turbo settings, affinity). Converted nanoseconds are estimates dependent on local hardware and measurement conditions.

### Calibration design

 - TSC ticks are converted to nanoseconds by sampling multiple `steady_clock` windows and computing an empirical cycles-per-nanosecond factor (recommend the median after rejecting outliers).
 - Calibration reduces the impact of TSC frequency assumptions but cannot fully compensate for non-invariant TSCs, virtualization artifacts, or dynamic turbo/frequency changes. Verify TSC invariance where possible and consider falling back to `steady_clock` measurements if the platform cannot provide reliable TSC behavior.

## Why `RequestId` Exists

- `RequestId` provides a simple, monotonically increasing token that producers can poll to detect completion without sharing complex pointers across threads.
- It allows the benchmark to measure per-request completion reliably even when orders are re-identified (MODIFY/CANCEL) after insertion.

## Why `lastProcessedRequestId()` Exists

- The engine publishes the most recent processed request id as an atomic variable.
- Producers poll this variable (or wait on it) to detect completion of their submitted request without requiring synchronous RPC-style callbacks.

## Percentiles & Aggregation

- Percentiles are computed from the distribution of per-request cycle counts after calibration to wall time.
- Use reservoir sampling or full collection depending on memory availability; by default the harness collects all events to compute exact percentiles for short runs.

## Throughput Measurement

- Throughput is computed as total successful requests processed divided by total wall-time during the measurement stage.
- For single-request synchronous submission (submit-and-poll), throughput is roughly the inverse of mean latency; the benchmark can be extended to batched or fire-and-forget submission patterns for higher throughput measurements.

## Common Benchmark Pitfalls

- Not pinning threads: without affinity, scheduler migration creates variance.
- Background processes and CPU frequency scaling: disable turbo/scale governors for reproducible runs.
- Measuring wall-clock without calibration: clock resolution can mask microsecond or sub-microsecond variance.
- Counting warmup requests in final metrics: warmup must be excluded.

## Limitations

- Microbenchmarks do not account for network jitter, serialization, or real exchange topology.
- The single-producer assumption limits generalization to real multi-client deployments.
- Benchmark numbers are machine and OS dependent; they are useful for intra-machine comparisons and not for cross-machine claims.

## How to Reproduce Results

1. Build in `Release` and ensure no debug instrumentation is present.
2. Reserve CPU cores, disable frequency scaling, and minimize background work.
3. Run the benchmark harness with a warmup, then the measurement stage.

Example commands:

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
# Example pinned run (adjust cores to your system)
taskset -c 2 ./benchmark --warmup=10000 --runs=100000
```


## Additional Notes for Interviews

- Be prepared to explain why serialized TSC is chosen and how RDTSCP differs from RDTSC.
- Explain how the completion token avoids cross-thread pointer sharing and why it's a safe, minimal synchronization mechanism.
- Discuss how to extend the harness to measure hybrid workloads or batched submission for scalability profiling.

## Screenshot / Artifacts

To include a benchmark screenshot in the repository for recruiter presentation, place a PNG at `media/benchmark.png`. A placeholder SVG (`media/benchmark_placeholder.svg`) is included; replace it with a real capture when you have reproducible results.

Suggested steps to produce a reproducible capture (Linux):

```bash
# Build
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run benchmark and capture textual output
./benchmark > bench_output.txt

# Convert textual output to an image (requires ImageMagick)
convert -background white -fill black -font DejaVu-Sans-Mono -pointsize 10 \
	label:@bench_output.txt ../media/benchmark.png
```

Alternatively capture a terminal screenshot using your desktop environment's screenshot utility and save it to `media/benchmark.png`.

## System snapshot for reproducibility

Include a concise system snapshot with any published benchmark results. This helps readers interpret the numbers and reproduce them if desired. Suggested commands (Linux):

```bash
lscpu
cat /proc/cpuinfo | grep 'model name' | uniq
free -h
uname -a
gcc --version
cat /etc/os-release
# Record CMake build flags
cd build && grep CMAKE_CXX_FLAGS CMakeCache.txt || echo "inspect CMakeLists.txt"
```

Also record the exact benchmark command you used and any affinity settings (e.g., `taskset` or `pthread_setaffinity_np` core ids) and governor/turbo settings if modified.

