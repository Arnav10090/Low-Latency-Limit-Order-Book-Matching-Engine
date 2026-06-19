# Preservation Property Tests Documentation

## Overview

The `test_preservation.cpp` file implements Property 2 from the design document: **Preservation - Non-MSVC Compilation Behavior Unchanged**.

## Purpose

These tests verify that the preprocessor guard fix for MSVC does NOT affect compilation behavior on:
- Linux/GCC x86_64 builds
- Linux/Clang x86_64 builds  
- ARM64 architecture builds
- Other non-x86 architectures

## Expected Behavior

### On MSVC x86/x64 (Windows)
```
[SKIP] This is MSVC x86/x64 context (bug condition)
       Preservation tests target non-MSVC contexts
       Use test_msvc_bug_condition for MSVC testing
```
**Status**: Test skips (exit code 0)

### On Linux/GCC or Linux/Clang x86_64
```
[SUCCESS] All preservation tests PASSED

Preservation Property 2 Verified:
  - Non-MSVC compilation behavior is correct
  - GCC/Clang x86 TSC timing works as expected
  - Function signatures preserved
  - Calibration logic preserved
```
**Status**: All 8 tests pass (exit code 0)

**Key Verification Points**:
1. `__x86_64__` or `__i386__` macros are defined
2. TSC timing code is available and functional
3. `_mm_pause()`, `__rdtsc()`, `__rdtscp()` work correctly
4. Calibration uses 7 samples × 250ms windows
5. Benchmark output message: "cycle counts are primary; nanoseconds are calibrated estimates"

### On ARM64 or Non-x86 Architectures
```
[SUCCESS] All preservation tests PASSED

Preservation Property 2 Verified:
  - Non-MSVC compilation behavior is correct
  - Non-x86 fallback behavior works as expected
  - Function signatures preserved
  - Calibration logic preserved
```
**Status**: All 8 tests pass (exit code 0)

**Key Verification Points**:
1. x86 macros are NOT defined
2. TSC timing code is disabled (fallback to `std::chrono::steady_clock`)
3. `std::this_thread::yield()` used instead of `_mm_pause()`
4. Calibration returns `{use_tsc=false, ticks_per_ns=1.0}`
5. Benchmark output message: "using wall-clock (steady_clock) measurements (no TSC)"

## Test Coverage

### Test 1: GCC/Clang x86 TSC Detection
Verifies that GCC/Clang on x86 correctly enables TSC timing via `__x86_64__` or `__i386__` macros.

**Validates**: Requirement 3.1

### Test 2: Non-x86 Fallback Behavior
Verifies that non-x86 architectures correctly fall back to `std::chrono::steady_clock`.

**Validates**: Requirements 3.2, 3.4

### Test 3: Function Signature Preservation
Verifies that `pauseHint()`, `readTicksStart()`, `readTicksEnd()` are callable with correct signatures.

**Validates**: Requirement 3.5

### Test 4: TSC Calibration Logic Preservation
Verifies that TSC calibration uses 7 samples of 250ms windows and computes median correctly.

**Validates**: Requirement 3.3

### Test 5: Preprocessor Guard Evaluation
Verifies that the current guard `defined(__x86_64__) || defined(__i386__)` evaluates correctly for the platform.

**Validates**: Requirements 3.1, 3.2, 3.6

### Test 6: Timing Measurement Functionality
Verifies that timing measurements produce monotonic, sensible results.

**Validates**: Requirements 3.5, 3.8

### Test 7: Pause Hint Behavior
Verifies that `pauseHint()` uses `_mm_pause()` on x86 and `std::this_thread::yield()` on non-x86.

**Validates**: Requirements 3.2, 3.7

### Test 8: Expected Benchmark Output Message
Verifies that the expected benchmark output message matches the timing method in use.

**Validates**: Requirement 3.8

## Running the Tests

### Build the Test
```bash
cd build
cmake ..
cmake --build . --config Release --target test_preservation
```

### Run on Windows/MSVC (will skip)
```bash
.\Release\test_preservation.exe
```

### Run on Linux/GCC or Clang
```bash
./test_preservation
```

## Integration with Spec Workflow

These tests are part of Task 2: "Write preservation property tests (BEFORE implementing fix)"

**Before Fix**:
- Tests should PASS on Linux/GCC x86 (establishing baseline)
- Tests should PASS on ARM64 (establishing baseline)
- Tests should SKIP on Windows/MSVC (not the preservation test context)

**After Fix**:
- Tests should continue to PASS on Linux/GCC x86 (behavior preserved)
- Tests should continue to PASS on ARM64 (behavior preserved)
- Tests should continue to SKIP on Windows/MSVC (still not the preservation test context)

The preservation tests establish a baseline that the fix should NOT change. If these tests fail after applying the fix, it indicates the fix has unintended side effects on non-MSVC platforms.

## Property-Based Testing Approach

While this test is written as a traditional unit test, it embodies property-based testing principles:

1. **Property**: For all non-MSVC compilation contexts, behavior remains unchanged
2. **Test Strategy**: Verify behavior across different compiler/architecture combinations
3. **Coverage**: Tests cover the key behavioral invariants that must be preserved
4. **Baseline**: Tests establish baseline on unfixed code
5. **Regression Detection**: Tests detect if fix introduces unintended changes

For true property-based testing with random input generation, a framework like QuickCheck (C++), Hypothesis (Python), or fast-check (JavaScript) would be used. However, for preprocessor macro testing, explicit compilation context testing is more practical.

## Expected Test Results

| Platform | Compiler | Architecture | Expected Result |
|----------|----------|--------------|-----------------|
| Windows | MSVC | x86_64 | SKIP (bug condition context) |
| Windows | MSVC | x86 | SKIP (bug condition context) |
| Linux | GCC | x86_64 | PASS (TSC enabled) |
| Linux | Clang | x86_64 | PASS (TSC enabled) |
| Linux | GCC | ARM64 | PASS (fallback enabled) |
| macOS | Clang | ARM64 (M1) | PASS (fallback enabled) |

## Notes

- The test replicates the timing functions from `benchmark/bench.cpp` to test their behavior directly
- Calibration test takes ~1.75 seconds (7 × 250ms samples)
- The test is self-contained and doesn't require building the full matching engine
- Exit code 0 = success, exit code 1 = failure
