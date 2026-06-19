# MSVC TSC Preprocessor Fix Design

## Overview

This bugfix resolves the exclusion of Time Stamp Counter (TSC) timing code on Windows/MSVC builds by adding MSVC-specific architecture detection macros to preprocessor guards. The issue occurs because the current guards only check GCC/Clang macros (`__x86_64__`, `__i386__`), which MSVC does not define. By adding MSVC's equivalent macros (`_M_X64`, `_M_IX86`) to 5 preprocessor guard locations, TSC timing with RDTSC/RDTSCP intrinsics will be enabled on Windows/MSVC, eliminating 20-50ns overhead per measurement and restoring cycle-level timing resolution.

## Glossary

- **Bug_Condition (C)**: Compilation on Windows with MSVC for x86 architecture where preprocessor guards evaluate to false
- **Property (P)**: TSC timing code compiles and executes, providing cycle-level timing resolution
- **Preservation**: Existing behavior on Linux/GCC/Clang and non-x86 platforms remains unchanged
- **TSC (Time Stamp Counter)**: CPU hardware counter accessed via RDTSC/RDTSCP intrinsics for high-precision timing
- **RDTSC/RDTSCP**: x86 intrinsics for reading the CPU cycle counter (`__rdtsc`, `__rdtscp`)
- **Preprocessor Guard**: Conditional compilation directive (`#if defined(...)`) that determines which code is compiled
- **`__x86_64__` / `__i386__`**: GCC/Clang macros for 64-bit and 32-bit x86 architectures
- **`_M_X64` / `_M_IX86`**: MSVC macros for 64-bit and 32-bit x86 architectures
- **`pauseHint()`**: Function in `benchmark/bench.cpp` that issues CPU pipeline pause hint (`_mm_pause` on x86)
- **`readTicksStart()`**: Function in `benchmark/bench.cpp` that reads TSC at measurement start with LFENCE barrier
- **`readTicksEnd()`**: Function in `benchmark/bench.cpp` that reads TSC at measurement end with RDTSCP/LFENCE
- **`calibrateTicks()`**: Function in `benchmark/bench.cpp` that calibrates TSC frequency against steady_clock
- **`engineLoop()`**: Main loop in `src/engine_thread.cpp` that uses pause hint during spin-waiting

## Bug Details

### Bug Condition

The bug manifests when compiling the codebase on Windows with the MSVC compiler for x86 (32-bit or 64-bit) architecture. The preprocessor guards at 5 locations check only GCC/Clang architecture macros, causing the TSC timing code to be excluded even though the target architecture supports TSC intrinsics.

**Formal Specification:**
```
FUNCTION isBugCondition(compilation_context)
  INPUT: compilation_context of type {compiler: string, platform: string, architecture: string}
  OUTPUT: boolean
  
  RETURN compilation_context.compiler == "MSVC"
         AND compilation_context.platform == "Windows"
         AND compilation_context.architecture IN ["x86", "x86_64"]
         AND NOT preprocessor_defined("__x86_64__")
         AND NOT preprocessor_defined("__i386__")
         AND preprocessor_defined("_M_X64") OR preprocessor_defined("_M_IX86")
END FUNCTION
```

### Examples

- **Compiling on Windows with MSVC for 64-bit x86**: The guard `#if defined(__x86_64__) || defined(__i386__)` evaluates to false because MSVC defines `_M_X64` instead. Expected: TSC code compiles. Actual: Falls back to `std::chrono::steady_clock`.

- **Compiling on Windows with MSVC for 32-bit x86**: The guard evaluates to false because MSVC defines `_M_IX86` instead. Expected: TSC code compiles. Actual: Falls back to `std::chrono::steady_clock`.

- **Benchmark output on Windows/MSVC**: Displays "using wall-clock (steady_clock) measurements (no TSC)" instead of "cycle counts are primary; nanoseconds are calibrated estimates". Expected: TSC calibration message.

- **Edge case - Windows with Clang-cl**: If using Clang in MSVC-compatible mode (clang-cl), both GCC-style and MSVC-style macros may be defined, so the guard should work correctly after the fix.

## Expected Behavior

### Preservation Requirements

**Unchanged Behaviors:**
- Linux/GCC/Clang builds must continue to detect x86 architecture via `__x86_64__` or `__i386__` and compile TSC code
- Non-x86 architectures (ARM, RISC-V, etc.) must continue to fall back to `std::chrono::steady_clock` and `std::this_thread::yield()`
- TSC calibration logic (median of 7 steady_clock windows, 250ms each) must remain unchanged
- Function signatures, return types, and calling conventions for `pauseHint`, `readTicksStart`, `readTicksEnd`, `calibrateTicks` must remain unchanged
- Benchmark statistics calculations (percentiles, throughput) and output formatting must remain unchanged
- Engine loop spin-wait behavior using `_mm_pause()` must continue to work identically

**Scope:**
All compilation contexts that do NOT involve Windows/MSVC for x86 architecture should be completely unaffected by this fix. This includes:
- Linux builds with GCC or Clang
- macOS builds
- Non-x86 architectures on any platform
- MinGW or Cygwin builds on Windows (which use GCC macros)

## Hypothesized Root Cause

Based on the bug description and source code analysis, the root cause is:

1. **Incomplete Compiler Coverage**: The preprocessor guards were written with only GCC/Clang in mind, not accounting for MSVC's different architecture macro names. This is a common portability oversight when code is developed primarily on Linux/GCC.

2. **MSVC Uses Different Macro Names**: MSVC defines `_M_X64` for 64-bit x86 and `_M_IX86` for 32-bit x86, while GCC/Clang define `__x86_64__` and `__i386__` respectively. The guards only check the latter.

3. **Five Affected Locations**: The issue appears in:
   - `benchmark/bench.cpp` line ~24: `#include <immintrin.h>` guard
   - `benchmark/bench.cpp` line ~73: `pauseHint()` implementation
   - `benchmark/bench.cpp` line ~81: `readTicksStart()` implementation
   - `benchmark/bench.cpp` line ~91: `readTicksEnd()` implementation
   - `benchmark/bench.cpp` line ~101: `calibrateTicks()` implementation
   - `src/engine_thread.cpp` line ~17: `#include <immintrin.h>` guard
   - `src/engine_thread.cpp` line ~107: `_mm_pause()` in `engineLoop()`

4. **Silent Fallback**: The code gracefully falls back to `std::chrono` alternatives, so the bug doesn't cause compilation errors or crashes—it just degrades performance and loses timing precision.

## Correctness Properties

Property 1: Bug Condition - MSVC Builds Enable TSC Timing

_For any_ compilation context where the compiler is MSVC, the platform is Windows, and the architecture is x86 or x86_64, the updated preprocessor guards SHALL evaluate to true, causing the TSC timing code (RDTSC/RDTSCP intrinsics, `_mm_pause`, `<immintrin.h>` include) to be compiled and executed, resulting in cycle-level timing resolution and the benchmark output displaying "cycle counts are primary; nanoseconds are calibrated estimates".

**Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5, 2.6**

Property 2: Preservation - Non-MSVC Compilation Behavior

_For any_ compilation context where the compiler is NOT MSVC (GCC, Clang) OR the architecture is NOT x86/x86_64 (ARM, RISC-V, etc.), the updated preprocessor guards SHALL produce exactly the same compilation and runtime behavior as the original guards, preserving TSC timing on GCC/Clang x86 builds and fallback behavior on non-x86 platforms.

**Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8**

## Fix Implementation

### Changes Required

Assuming our root cause analysis is correct (incomplete compiler-specific macro coverage):

**Files**: `benchmark/bench.cpp` and `src/engine_thread.cpp`

**Specific Changes**:

1. **Update `<immintrin.h>` Include Guard in `benchmark/bench.cpp` (line ~24)**:
   - Current: `#if defined(__x86_64__) || defined(__i386__)`
   - Fixed: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
   - Ensures MSVC can access RDTSC/RDTSCP intrinsics

2. **Update `pauseHint()` Implementation Guard in `benchmark/bench.cpp` (line ~73)**:
   - Current: `#if defined(__x86_64__) || defined(__i386__)`
   - Fixed: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
   - Enables `_mm_pause()` on MSVC builds

3. **Update `readTicksStart()` Implementation Guard in `benchmark/bench.cpp` (line ~81)**:
   - Current: `#if defined(__x86_64__) || defined(__i386__)`
   - Fixed: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
   - Enables LFENCE + RDTSC timing on MSVC builds

4. **Update `readTicksEnd()` Implementation Guard in `benchmark/bench.cpp` (line ~91)**:
   - Current: `#if defined(__x86_64__) || defined(__i386__)`
   - Fixed: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
   - Enables RDTSCP + LFENCE timing on MSVC builds

5. **Update `calibrateTicks()` Implementation Guard in `benchmark/bench.cpp` (line ~101)**:
   - Current: `#if defined(__x86_64__) || defined(__i386__)`
   - Fixed: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
   - Enables TSC calibration on MSVC builds

6. **Update `<immintrin.h>` Include Guard in `src/engine_thread.cpp` (line ~17)**:
   - Current: `#if defined(__x86_64__) || defined(__i386__)`
   - Fixed: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
   - Ensures MSVC can access `_mm_pause()` intrinsic

7. **Update `_mm_pause()` Guard in `engineLoop()` in `src/engine_thread.cpp` (line ~107)**:
   - Current: `#if defined(__x86_64__) || defined(__i386__)`
   - Fixed: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
   - Enables pause hint in spin-wait on MSVC builds

**Implementation Pattern**: All 7 guards follow the same pattern—append `|| defined(_M_X64) || defined(_M_IX86)` to the existing condition. No other code changes are required.

## Testing Strategy

### Validation Approach

The testing strategy follows a two-phase approach: first, verify the bug exists on unfixed code by observing fallback behavior on Windows/MSVC, then verify the fix enables TSC timing and preserves existing behavior on other platforms.

### Exploratory Bug Condition Checking

**Goal**: Surface counterexamples that demonstrate the bug BEFORE implementing the fix. Confirm that Windows/MSVC builds currently fall back to `std::chrono::steady_clock`.

**Test Plan**: Compile and run the benchmark on Windows with MSVC before applying the fix. Observe the benchmark output message and timing overhead to confirm TSC code is NOT being used.

**Test Cases**:
1. **MSVC 64-bit Build Test**: Compile with MSVC for x64 target, run benchmark, verify output shows "using wall-clock (steady_clock) measurements (no TSC)" (will fail on unfixed code—this is the bug)
2. **MSVC 32-bit Build Test**: Compile with MSVC for x86 target, run benchmark, verify output shows wall-clock fallback message (will fail on unfixed code)
3. **Preprocessor Macro Check**: Write a small test program that prints the values of `__x86_64__`, `__i386__`, `_M_X64`, `_M_IX86` on MSVC to confirm which macros are defined (will show `_M_X64` or `_M_IX86` defined, but not GCC macros)
4. **Performance Overhead Test**: Compare latency measurements on MSVC build vs. GCC/Clang build to observe 20-50ns overhead (may show higher latencies on MSVC due to fallback)

**Expected Counterexamples**:
- MSVC builds display "using wall-clock (steady_clock) measurements (no TSC)" instead of "cycle counts are primary"
- Preprocessor test shows `_M_X64` or `_M_IX86` defined on MSVC, but `__x86_64__` and `__i386__` undefined
- Possible causes: missing MSVC macro checks, guards only checking GCC/Clang macros

### Fix Checking

**Goal**: Verify that for all compilation contexts where the bug condition holds (Windows/MSVC/x86), the fixed preprocessor guards enable TSC timing.

**Pseudocode:**
```
FOR ALL compilation_context WHERE isBugCondition(compilation_context) DO
  compiled_code := compile_with_fixed_guards(compilation_context)
  ASSERT TSC_timing_enabled(compiled_code)
  ASSERT benchmark_output_contains("cycle counts are primary")
  ASSERT NOT benchmark_output_contains("using wall-clock")
END FOR
```

**Test Plan**: After applying the fix, compile and run the benchmark on Windows/MSVC for both 64-bit and 32-bit targets. Verify the output message confirms TSC is active.

**Test Cases**:
1. **MSVC 64-bit Fixed Build**: Compile with MSVC x64, verify output shows "cycle counts are primary; nanoseconds are calibrated estimates"
2. **MSVC 32-bit Fixed Build**: Compile with MSVC x86, verify output shows TSC calibration message
3. **Calibration Factor Displayed**: Verify benchmark output displays "Calib factor: X.XXXXXX cycles/ns (median)" on MSVC builds
4. **TSC Functions Callable**: Write unit tests that call `readTicksStart()`, `readTicksEnd()`, `pauseHint()`, `calibrateTicks()` on MSVC and verify they use TSC code paths (not fallback)
5. **Intrinsics Available**: Verify `<immintrin.h>` is included and `_mm_pause()`, `__rdtsc()`, `__rdtscp()` are available on MSVC builds

### Preservation Checking

**Goal**: Verify that for all compilation contexts where the bug condition does NOT hold (Linux/GCC/Clang or non-x86 architectures), the fixed guards produce the same behavior as the original guards.

**Pseudocode:**
```
FOR ALL compilation_context WHERE NOT isBugCondition(compilation_context) DO
  original_compiled_code := compile_with_original_guards(compilation_context)
  fixed_compiled_code := compile_with_fixed_guards(compilation_context)
  ASSERT original_compiled_code.behavior == fixed_compiled_code.behavior
  ASSERT original_compiled_code.benchmark_output == fixed_compiled_code.benchmark_output
END FOR
```

**Testing Approach**: Property-based testing is recommended for preservation checking because:
- It can generate many compilation contexts automatically (different compilers, platforms, architectures)
- It catches edge cases like cross-compilation scenarios or unusual compiler configurations
- It provides strong guarantees that behavior is unchanged across the entire input domain

**Test Plan**: Compile and run benchmarks on Linux/GCC, Linux/Clang, and non-x86 platforms before and after the fix. Observe that behavior is identical.

**Test Cases**:
1. **GCC x86_64 Preservation**: Compile on Linux with GCC for x86_64, verify TSC timing works identically before and after fix (output message, calibration factor, timing resolution)
2. **Clang x86_64 Preservation**: Compile on Linux with Clang for x86_64, verify identical TSC behavior
3. **ARM64 Fallback Preservation**: Compile for ARM64 architecture, verify fallback to `std::chrono::steady_clock` and `std::this_thread::yield()` works identically before and after fix
4. **Non-x86 Output Preservation**: Verify benchmark output on ARM/RISC-V shows "using wall-clock (steady_clock) measurements (no TSC)" consistently
5. **Function Signature Preservation**: Write tests that verify function signatures (parameter types, return types) for `pauseHint`, `readTicksStart`, `readTicksEnd`, `calibrateTicks` remain unchanged
6. **Calibration Logic Preservation**: Verify TSC calibration uses 7 samples of 250ms windows and computes median on all platforms where TSC is enabled
7. **Engine Loop Pause Preservation**: Verify `engineLoop()` spin-wait behavior with `_mm_pause()` on x86 and `std::this_thread::yield()` on non-x86 remains unchanged

### Unit Tests

- Test preprocessor macro detection: Write tests that verify the correct macros are defined on each compiler/platform combination
- Test TSC function behavior on MSVC: Call timing functions and verify they return reasonable values (non-zero tick counts, calibration factor > 0)
- Test fallback behavior on non-x86: Verify timing functions use `std::chrono` on ARM/RISC-V
- Test pause hint behavior: Verify `pauseHint()` and engine loop spin-wait use correct implementation based on architecture

### Property-Based Tests

- Generate random compilation contexts (compiler, platform, architecture combinations) and verify guards evaluate correctly
- Generate random timing measurements and verify TSC timing produces monotonically increasing tick counts
- Generate random benchmark workloads and verify output messages match the expected timing method (TSC vs. wall-clock)

### Integration Tests

- Full benchmark run on Windows/MSVC x64: Verify end-to-end TSC timing works correctly with calibration, statistics, and output formatting
- Full benchmark run on Linux/GCC x64: Verify behavior is preserved after fix
- Cross-platform comparison: Run same workload on Windows/MSVC and Linux/GCC, verify both use TSC timing and produce comparable results
- Visual verification: Manually inspect benchmark output on different platforms to confirm correct timing method is reported
