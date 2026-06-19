**Benchmark Results (replace with your verified numbers)**
- System snapshot: See `hardware_cpu.txt`, `cmake_version.txt`, `compiler_version.txt` in repo root (produced by `docs/windows_benchmark_instructions.ps1`).
- Calibration factor: `<cycles_per_ns>` (printed by `benchmark` tool; use median calibration).

**Benchmark (Release build)**
- Command: See `docs/windows_benchmark_instructions.ps1`.
- Mean latency (p50/p95/p99/p99.9):
  - Mean: <mean> ns (calibrated from cycles)
  - p50: <p50> ns
  - p95: <p95> ns
  - p99: <p99> ns
  - p99.9: <p99.9> ns
- Throughput (orders/sec):
  - Unpinned: <unpinned_ops_per_sec>
  - Pinned: <pinned_ops_per_sec>

**Notes**
- Cycle counts are the canonical measurement; nanoseconds are estimated using the printed calibration factor.
- Include `bench_output.txt` and the printed calibration factor when publishing results so others can validate your run. The default harness records calibrated nanoseconds and prints the calibration factor; if you need raw cycle dumps, modify the harness to persist raw tick deltas.
- Replace `media/benchmark.png` with an image produced by the provided PowerShell script after running the benchmark on your hardware.
