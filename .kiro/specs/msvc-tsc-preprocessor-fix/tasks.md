# Implementation Plan

## Overview

This task plan follows the bugfix exploratory workflow to fix the MSVC TSC preprocessor issue. The approach is:
1. **Explore** - Write tests BEFORE fix to understand the bug (Bug Condition test that MUST fail on unfixed code)
2. **Preserve** - Write tests for non-buggy behavior (Preservation tests that MUST pass on unfixed code)
3. **Implement** - Apply the preprocessor guard fixes with understanding (update 7 guards to include MSVC macros)
4. **Validate** - Verify fix works and doesn't break anything (re-run same tests, expect different outcomes)

The fix adds MSVC-specific architecture macros (`_M_X64`, `_M_IX86`) to preprocessor guards that currently only check GCC/Clang macros (`__x86_64__`, `__i386__`), enabling TSC timing on Windows/MSVC builds.

## Tasks

- [x] 1. Write bug condition exploration test
  - **Property 1: Bug Condition** - MSVC Preprocessor Guards Exclude TSC Code
  - **CRITICAL**: This test MUST FAIL on unfixed code - failure confirms the bug exists
  - **DO NOT attempt to fix the test or the code when it fails**
  - **NOTE**: This test encodes the expected behavior - it will validate the fix when it passes after implementation
  - **GOAL**: Surface counterexamples that demonstrate the bug exists on Windows/MSVC builds
  - **Scoped PBT Approach**: Scope the property to concrete failing case - Windows/MSVC compilation with x86/x86_64 architecture
  - Test that when compiling with MSVC on Windows for x86/x86_64 architecture:
    - The preprocessor guards `#if defined(__x86_64__) || defined(__i386__)` evaluate to FALSE
    - The macros `_M_X64` or `_M_IX86` ARE defined (MSVC x86 detection)
    - The macros `__x86_64__` and `__i386__` are NOT defined (GCC/Clang only)
    - TSC timing code (`<immintrin.h>` include, `_mm_pause()`, RDTSC intrinsics) is excluded from compilation
    - Benchmark output displays "using wall-clock (steady_clock) measurements (no TSC)" instead of "cycle counts are primary; nanoseconds are calibrated estimates"
  - Run test on UNFIXED code (before applying preprocessor guard changes)
  - **EXPECTED OUTCOME**: Test FAILS (this is correct - it proves the bug exists)
  - Document counterexamples found:
    - MSVC x64 build: `_M_X64` defined but guards exclude TSC code → fallback to `std::chrono::steady_clock`
    - MSVC x86 build: `_M_IX86` defined but guards exclude TSC code → fallback to `std::chrono::steady_clock`
    - Benchmark output confirms TSC timing is not active
  - Mark task complete when test is written, run, and failure is documented
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5_

- [x] 2. Write preservation property tests (BEFORE implementing fix)
  - **Property 2: Preservation** - Non-MSVC Compilation Behavior Unchanged
  - **IMPORTANT**: Follow observation-first methodology
  - Observe behavior on UNFIXED code for non-buggy compilation contexts:
    - Linux/GCC x86_64: TSC timing code compiles and executes (guards evaluate to true via `__x86_64__`)
    - Linux/Clang x86_64: TSC timing code compiles and executes (guards evaluate to true via `__x86_64__`)
    - ARM64 architecture: TSC timing code excluded, fallback to `std::chrono::steady_clock` (guards correctly evaluate to false)
    - Non-x86 architectures: Fallback path used (`std::this_thread::yield()` in `pauseHint()`)
  - Write property-based tests capturing observed behavior patterns:
    - For all GCC/Clang x86 compilation contexts: TSC timing code SHALL compile and benchmark output SHALL display "cycle counts are primary"
    - For all non-x86 architectures: Fallback code SHALL compile and benchmark output SHALL display "using wall-clock (steady_clock) measurements (no TSC)"
    - Function signatures for `pauseHint`, `readTicksStart`, `readTicksEnd`, `calibrateTicks` SHALL remain unchanged
    - TSC calibration logic SHALL use median of 7 samples with 250ms windows
    - Engine loop spin-wait SHALL use `_mm_pause()` on x86 and `std::this_thread::yield()` on non-x86
  - Property-based testing generates many test cases for stronger guarantees
  - Run tests on UNFIXED code
  - **EXPECTED OUTCOME**: Tests PASS (this confirms baseline behavior to preserve)
  - Mark task complete when tests are written, run, and passing on unfixed code
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8_

- [x] 3. Fix preprocessor guards to enable TSC timing on Windows/MSVC

  - [x] 3.1 Update preprocessor guard for `<immintrin.h>` include in `benchmark/bench.cpp` (line ~24)
    - Change: `#if defined(__x86_64__) || defined(__i386__)`
    - To: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
    - Ensures MSVC can access RDTSC/RDTSCP intrinsics
    - _Bug_Condition: isBugCondition(context) where context.compiler == "MSVC" AND context.platform == "Windows" AND context.architecture IN ["x86", "x86_64"]_
    - _Expected_Behavior: Guard evaluates to true on MSVC, enabling TSC timing code compilation_
    - _Preservation: GCC/Clang x86 builds continue to work via `__x86_64__`/`__i386__`, non-x86 architectures continue to use fallback_
    - _Requirements: 2.1, 2.2, 3.1_

  - [x] 3.2 Update preprocessor guard for `pauseHint()` implementation in `benchmark/bench.cpp` (line ~73)
    - Change: `#if defined(__x86_64__) || defined(__i386__)`
    - To: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
    - Enables `_mm_pause()` on MSVC builds
    - _Bug_Condition: isBugCondition(context) for MSVC Windows x86 compilation_
    - _Expected_Behavior: `_mm_pause()` compiles on MSVC instead of `std::this_thread::yield()` fallback_
    - _Preservation: Existing behavior on GCC/Clang x86 and non-x86 platforms unchanged_
    - _Requirements: 2.1, 2.2, 3.5_

  - [x] 3.3 Update preprocessor guard for `readTicksStart()` implementation in `benchmark/bench.cpp` (line ~81)
    - Change: `#if defined(__x86_64__) || defined(__i386__)`
    - To: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
    - Enables LFENCE + RDTSC timing on MSVC builds
    - _Bug_Condition: isBugCondition(context) for MSVC Windows x86 compilation_
    - _Expected_Behavior: High-precision TSC start timing on MSVC instead of `std::chrono::steady_clock`_
    - _Preservation: Existing timing behavior on GCC/Clang x86 and fallback on non-x86 unchanged_
    - _Requirements: 2.1, 2.2, 2.4, 3.5_

  - [x] 3.4 Update preprocessor guard for `readTicksEnd()` implementation in `benchmark/bench.cpp` (line ~91)
    - Change: `#if defined(__x86_64__) || defined(__i386__)`
    - To: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
    - Enables RDTSCP + LFENCE timing on MSVC builds
    - _Bug_Condition: isBugCondition(context) for MSVC Windows x86 compilation_
    - _Expected_Behavior: High-precision TSC end timing on MSVC with serialization_
    - _Preservation: Existing timing behavior on GCC/Clang x86 and fallback on non-x86 unchanged_
    - _Requirements: 2.1, 2.2, 2.4, 3.5_

  - [x] 3.5 Update preprocessor guard for `calibrateTicks()` implementation in `benchmark/bench.cpp` (line ~101)
    - Change: `#if defined(__x86_64__) || defined(__i386__)`
    - To: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
    - Enables TSC calibration on MSVC builds
    - _Bug_Condition: isBugCondition(context) for MSVC Windows x86 compilation_
    - _Expected_Behavior: TSC frequency calibration active on MSVC, returning `{true, ticks_per_ns}`_
    - _Preservation: Calibration logic (7 samples, 250ms windows, median) unchanged, fallback `{false, 1.0}` on non-x86 unchanged_
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 3.3_

  - [x] 3.6 Update preprocessor guard for `<immintrin.h>` include in `src/engine_thread.cpp` (line ~17)
    - Change: `#if defined(__x86_64__) || defined(__i386__)`
    - To: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
    - Ensures MSVC can access `_mm_pause()` intrinsic
    - _Bug_Condition: isBugCondition(context) for MSVC Windows x86 compilation_
    - _Expected_Behavior: `<immintrin.h>` included on MSVC, enabling intrinsics_
    - _Preservation: GCC/Clang x86 builds continue to include header, non-x86 builds unaffected_
    - _Requirements: 2.1, 2.2, 3.1_

  - [x] 3.7 Update preprocessor guard for `_mm_pause()` in `engineLoop()` in `src/engine_thread.cpp` (line ~107)
    - Change: `#if defined(__x86_64__) || defined(__i386__)`
    - To: `#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)`
    - Enables pause hint in spin-wait on MSVC builds
    - _Bug_Condition: isBugCondition(context) for MSVC Windows x86 compilation_
    - _Expected_Behavior: `_mm_pause()` used in engine loop on MSVC instead of `std::this_thread::yield()`_
    - _Preservation: Engine loop spin-wait behavior on GCC/Clang x86 unchanged, non-x86 fallback unchanged_
    - _Requirements: 2.1, 2.2, 2.6, 3.7_

  - [x] 3.8 Verify bug condition exploration test now passes
    - **Property 1: Expected Behavior** - TSC Timing Enabled on MSVC
    - **IMPORTANT**: Re-run the SAME test from task 1 - do NOT write a new test
    - The test from task 1 encodes the expected behavior
    - When this test passes, it confirms the expected behavior is satisfied
    - Run bug condition exploration test from step 1 on FIXED code
    - Verify that on Windows/MSVC x86/x86_64 builds:
      - Preprocessor guards now evaluate to TRUE (via `_M_X64` or `_M_IX86`)
      - TSC timing code compiles and executes
      - Benchmark output displays "cycle counts are primary; nanoseconds are calibrated estimates"
      - Calibration factor is displayed (e.g., "Calib factor: X.XXXXXX cycles/ns (median)")
      - No fallback to `std::chrono::steady_clock`
    - **EXPECTED OUTCOME**: Test PASSES (confirms bug is fixed)
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6_

  - [x] 3.9 Verify preservation tests still pass
    - **Property 2: Preservation** - Non-MSVC Compilation Behavior Unchanged
    - **IMPORTANT**: Re-run the SAME tests from task 2 - do NOT write new tests
    - Run preservation property tests from step 2 on FIXED code
    - Verify that on non-MSVC compilation contexts:
      - Linux/GCC x86_64: TSC timing still works, output shows "cycle counts are primary"
      - Linux/Clang x86_64: TSC timing still works identically
      - ARM64/non-x86: Fallback behavior unchanged, output shows "using wall-clock"
      - Function signatures, calibration logic, benchmark statistics all unchanged
      - Engine loop spin-wait behavior unchanged
    - **EXPECTED OUTCOME**: Tests PASS (confirms no regressions)
    - Confirm all tests still pass after fix (no regressions)
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8_

- [x] 4. Checkpoint - Ensure all tests pass
  - Verify bug condition test passes (TSC timing enabled on MSVC)
  - Verify preservation tests pass (behavior unchanged on GCC/Clang and non-x86)
  - Confirm benchmark output is correct on all platforms:
    - Windows/MSVC: "cycle counts are primary; nanoseconds are calibrated estimates"
    - Linux/GCC/Clang x86: "cycle counts are primary; nanoseconds are calibrated estimates"
    - Non-x86 architectures: "using wall-clock (steady_clock) measurements (no TSC)"
  - Ensure all preprocessor guards are consistent across both files
  - Ask the user if questions arise

## Task Dependency Graph

```json
{
  "waves": [
    {
      "name": "Wave 1: Exploration Test (Pre-Fix)",
      "tasks": ["1"],
      "description": "Write bug condition test that MUST fail on unfixed code"
    },
    {
      "name": "Wave 2: Preservation Tests (Pre-Fix)",
      "tasks": ["2"],
      "description": "Write preservation tests that MUST pass on unfixed code"
    },
    {
      "name": "Wave 3: Implementation",
      "tasks": ["3.1", "3.2", "3.3", "3.4", "3.5", "3.6", "3.7"],
      "description": "Apply preprocessor guard fixes (can execute in parallel)"
    },
    {
      "name": "Wave 4: Validation",
      "tasks": ["3.8", "3.9"],
      "description": "Re-run tests on fixed code and verify results"
    },
    {
      "name": "Wave 5: Checkpoint",
      "tasks": ["4"],
      "description": "Final validation and verification"
    }
  ]
}
```

```
Task 1 (Bug Condition Test - MUST fail on unfixed code)
  ↓
Task 2 (Preservation Tests - MUST pass on unfixed code)
  ↓
Task 3 (Implementation)
  ├── Task 3.1 (Fix bench.cpp immintrin.h include guard)
  ├── Task 3.2 (Fix bench.cpp pauseHint guard)
  ├── Task 3.3 (Fix bench.cpp readTicksStart guard)
  ├── Task 3.4 (Fix bench.cpp readTicksEnd guard)
  ├── Task 3.5 (Fix bench.cpp calibrateTicks guard)
  ├── Task 3.6 (Fix engine_thread.cpp immintrin.h include guard)
  ├── Task 3.7 (Fix engine_thread.cpp _mm_pause guard)
  ↓
Task 3.8 (Verify bug condition test now PASSES)
  ↓
Task 3.9 (Verify preservation tests still PASS)
  ↓
Task 4 (Checkpoint)
```

**Key Dependencies:**
- Tasks 3.1-3.7 can be executed in parallel (all are independent preprocessor guard updates)
- Task 3.8 depends on ALL of tasks 3.1-3.7 being complete
- Task 3.9 depends on task 3.8 passing
- Task 4 depends on tasks 3.8 and 3.9 passing

## Notes

### Property-Based Testing Strategy

This bugfix uses property-based testing (PBT) to validate both the fix and preservation of existing behavior:

1. **Bug Condition Property (Task 1)**:
   - Scoped to concrete failing case: Windows/MSVC builds on x86/x86_64 architecture
   - Property: When `_M_X64` or `_M_IX86` is defined AND `__x86_64__` and `__i386__` are not defined, TSC timing code should be excluded (on unfixed code)
   - Expected: Test FAILS on unfixed code (confirms bug exists)
   - After fix: Test PASSES (confirms TSC timing is now enabled)

2. **Preservation Properties (Task 2)**:
   - Property: For all non-MSVC compilation contexts (Linux/GCC, Linux/Clang, ARM64), behavior SHALL remain unchanged
   - Expected: Tests PASS on unfixed code (captures baseline behavior)
   - After fix: Tests still PASS (confirms no regressions)

### Testing Platforms

To fully validate this fix, test on:
- **Windows/MSVC x64** (primary bug target)
- **Windows/MSVC x86** (secondary bug target)
- **Linux/GCC x86_64** (preservation validation)
- **Linux/Clang x86_64** (preservation validation)
- **ARM64/aarch64** (preservation validation - fallback path)

### Preprocessor Macro Reference

| Macro | Compiler | Architecture | Purpose |
|-------|----------|--------------|---------|
| `__x86_64__` | GCC/Clang | x86-64 | Existing guard (GCC/Clang detection) |
| `__i386__` | GCC/Clang | x86-32 | Existing guard (GCC/Clang detection) |
| `_M_X64` | MSVC | x86-64 | NEW - MSVC x64 detection (this fix) |
| `_M_IX86` | MSVC | x86-32 | NEW - MSVC x86 detection (this fix) |

### Expected Benchmark Output Changes

**Before Fix (Windows/MSVC):**
```
using wall-clock (steady_clock) measurements (no TSC)
```

**After Fix (Windows/MSVC):**
```
Calib factor: 3.456789 cycles/ns (median of 7 samples)
cycle counts are primary; nanoseconds are calibrated estimates
```

### Files Modified

1. `benchmark/bench.cpp` - 5 preprocessor guards updated (lines ~24, ~73, ~81, ~91, ~101)
2. `src/engine_thread.cpp` - 2 preprocessor guards updated (lines ~17, ~107)

### Implementation Notes

- All 7 guard updates follow the same pattern: add `|| defined(_M_X64) || defined(_M_IX86)` to existing conditions
- No functional code changes - only preprocessor conditionals
- Zero impact on runtime performance
- No changes to function signatures, APIs, or calibration logic
- Maintains full backward compatibility with GCC/Clang and non-x86 platforms
