# Bugfix Requirements Document

## Introduction

On Windows with the MSVC compiler, the Time Stamp Counter (TSC) timing code using RDTSC/RDTSCP intrinsics is excluded from compilation due to incomplete preprocessor macro detection. The existing x86 architecture detection guards only check for GCC/Clang-specific macros (`__x86_64__` and `__i386__`), which are not defined by MSVC. As a result, the system falls back to `std::chrono::steady_clock`, introducing 20-50 nanoseconds of overhead per measurement and losing cycle-level timing resolution.

This bugfix adds MSVC-specific preprocessor macros (`_M_X64` for 64-bit x86 and `_M_IX86` for 32-bit x86) to the architecture detection guards, enabling TSC timing on Windows/MSVC builds.

## Bug Analysis

### Current Behavior (Defect)

1.1 WHEN compiling on Windows with MSVC for 64-bit x86 architecture THEN the preprocessor guard `#if defined(__x86_64__) || defined(__i386__)` evaluates to false because MSVC does not define these macros

1.2 WHEN the preprocessor guard evaluates to false THEN the TSC timing functions (`pauseHint`, `readTicksStart`, `readTicksEnd`, `calibrateTicks`) fall back to their non-TSC implementations using `std::chrono::steady_clock`

1.3 WHEN TSC timing code is excluded from compilation THEN benchmark output displays "using wall-clock (steady_clock) measurements (no TSC)" indicating fallback behavior

1.4 WHEN `std::chrono::steady_clock` is used instead of RDTSC/RDTSCP THEN each timing measurement incurs 20-50 nanoseconds of additional overhead

1.5 WHEN compiling on Windows with MSVC for 32-bit x86 architecture THEN the same fallback behavior occurs because `_M_IX86` is also not checked

### Expected Behavior (Correct)

2.1 WHEN compiling on Windows with MSVC for 64-bit x86 architecture THEN the preprocessor guard SHALL include `_M_X64` macro check so the condition evaluates to true

2.2 WHEN the preprocessor guard evaluates to true on MSVC THEN the TSC timing code using RDTSC/RDTSCP intrinsics SHALL be compiled and executed

2.3 WHEN TSC timing code is compiled on Windows/MSVC THEN benchmark output SHALL display "cycle counts are primary; nanoseconds are calibrated estimates" indicating TSC is active

2.4 WHEN TSC timing is active THEN timing measurements SHALL use direct CPU cycle counters with minimal overhead (sub-nanosecond accuracy)

2.5 WHEN compiling on Windows with MSVC for 32-bit x86 architecture THEN the preprocessor guard SHALL include `_M_IX86` macro check so the condition evaluates to true

2.6 WHEN all 5 preprocessor guards in `benchmark/bench.cpp` (lines ~73, ~81, ~91, ~101) and `src/engine_thread.cpp` (line ~107) are updated THEN TSC timing SHALL be enabled across the entire codebase for MSVC builds

### Unchanged Behavior (Regression Prevention)

3.1 WHEN compiling on Linux with GCC or Clang for x86 architecture THEN the system SHALL CONTINUE TO detect x86 architecture via `__x86_64__` or `__i386__` macros and compile TSC timing code

3.2 WHEN compiling on non-x86 architectures (ARM, RISC-V, etc.) regardless of compiler THEN the system SHALL CONTINUE TO fall back to `std::chrono::steady_clock` and `std::this_thread::yield()`

3.3 WHEN TSC timing code is compiled THEN the system SHALL CONTINUE TO perform calibration using the median of 7 steady_clock windows (250ms each) to convert cycles to nanoseconds

3.4 WHEN the fallback path is used on non-x86 platforms THEN the system SHALL CONTINUE TO use `std::chrono::steady_clock` for timing measurements

3.5 WHEN any timing function (`pauseHint`, `readTicksStart`, `readTicksEnd`, `calibrateTicks`) is called THEN the function signatures, return types, and calling conventions SHALL CONTINUE TO remain unchanged

3.6 WHEN the preprocessor guards are updated THEN no other code logic, algorithms, or data structures SHALL be modified

3.7 WHEN `_mm_pause()` is used in the `engineLoop` spin-wait (src/engine_thread.cpp line ~107) THEN the behavior SHALL CONTINUE TO pause the CPU pipeline hint as before

3.8 WHEN benchmark statistics are calculated and displayed THEN the output format, percentile calculations, and throughput measurements SHALL CONTINUE TO work identically
