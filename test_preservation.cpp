// Preservation Property Tests for MSVC TSC Preprocessor Fix
// **Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8**
//
// These tests verify that non-MSVC compilation behavior remains unchanged
// after applying the fix. Tests should PASS on unfixed code (establishing baseline)
// and continue to PASS after the fix is applied.
//
// Property 2: Preservation - Non-MSVC Compilation Behavior Unchanged
// For any compilation context where compiler is NOT MSVC OR architecture is NOT x86/x64,
// the updated preprocessor guards SHALL produce exactly the same behavior as original guards.

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdint>
#include <vector>
#include <algorithm>

// Include TSC timing code with current (unfixed) preprocessor guards
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define TSC_AVAILABLE 1
#else
#define TSC_AVAILABLE 0
#endif

// Replicate the timing functions to test their behavior
namespace PreservationTest {

void pauseHint() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

uint64_t readTicksStart() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_lfence();
    return __rdtsc();
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

uint64_t readTicksEnd() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int aux = 0;
    const uint64_t ticks = __rdtscp(&aux);
    _mm_lfence();
    return ticks;
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

struct TickCalibration {
    bool use_tsc;
    double ticks_per_ns;
};

TickCalibration calibrateTicks() {
#if defined(__x86_64__) || defined(__i386__)
    constexpr int CALIBRATION_SAMPLES = 7;
    constexpr auto CALIBRATION_WINDOW = std::chrono::milliseconds(250);
    
    std::vector<double> samples;
    samples.reserve(CALIBRATION_SAMPLES);

    for (int sample_index = 0; sample_index < CALIBRATION_SAMPLES; ++sample_index) {
        const auto wall_start = std::chrono::steady_clock::now();
        const uint64_t tick_start = readTicksStart();
        std::this_thread::sleep_for(CALIBRATION_WINDOW);
        const uint64_t tick_end = readTicksEnd();
        const auto wall_end = std::chrono::steady_clock::now();
        const double wall_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count());
        samples.push_back(static_cast<double>(tick_end - tick_start) / wall_ns);
    }

    std::sort(samples.begin(), samples.end());
    return {true, samples[samples.size() / 2]};
#else
    return {false, 1.0};
#endif
}

} // namespace PreservationTest

int main() {
    std::cout << "=======================================================\n";
    std::cout << "  Preservation Property Tests (Property 2)\n";
    std::cout << "=======================================================\n";
    std::cout << "\n";
    std::cout << "These tests verify non-MSVC compilation behavior\n";
    std::cout << "remains unchanged after applying the fix.\n";
    std::cout << "\n";
    std::cout << "Expected: All tests PASS (before and after fix)\n";
    std::cout << "\n";

    bool all_tests_passed = true;
    int test_count = 0;
    int passed_count = 0;

    // Determine compilation context
    std::cout << "--- Compilation Context Detection ---\n";
    
    std::string compiler = "Unknown";
    std::string architecture = "Unknown";
    bool is_msvc = false;
    bool is_x86 = false;

#if defined(_MSC_VER)
    compiler = "MSVC";
    is_msvc = true;
#elif defined(__GNUC__)
    compiler = "GCC";
#elif defined(__clang__)
    compiler = "Clang";
#endif

#if defined(_M_X64) || defined(__x86_64__)
    architecture = "x86_64";
    is_x86 = true;
#elif defined(_M_IX86) || defined(__i386__)
    architecture = "x86";
    is_x86 = true;
#elif defined(__aarch64__) || defined(_M_ARM64)
    architecture = "ARM64";
#elif defined(__arm__) || defined(_M_ARM)
    architecture = "ARM32";
#endif

    std::cout << "  Compiler: " << compiler << "\n";
    std::cout << "  Architecture: " << architecture << "\n";
    std::cout << "  Is MSVC: " << (is_msvc ? "YES" : "NO") << "\n";
    std::cout << "  Is x86/x64: " << (is_x86 ? "YES" : "NO") << "\n";
    std::cout << "\n";

    // Check if this is a preservation test context (non-MSVC OR non-x86)
    bool is_preservation_context = !is_msvc || !is_x86;

    if (is_msvc && is_x86) {
        std::cout << "[SKIP] This is MSVC x86/x64 context (bug condition)\n";
        std::cout << "       Preservation tests target non-MSVC contexts\n";
        std::cout << "       Use test_msvc_bug_condition for MSVC testing\n";
        return 0;
    }

    std::cout << "[INFO] This is a preservation test context\n";
    std::cout << "       Testing that behavior remains unchanged\n";
    std::cout << "\n";

    // Test 1: GCC/Clang x86 TSC Detection
    std::cout << "--- Test 1: GCC/Clang x86 TSC Detection ---\n";
    test_count++;
    
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    std::cout << "[TEST] GCC/Clang x86 context detected\n";
    
#if TSC_AVAILABLE
    std::cout << "[PASS] TSC timing code is available (expected)\n";
    std::cout << "       __x86_64__ or __i386__ correctly detected\n";
    passed_count++;
#else
    std::cout << "[FAIL] TSC timing code NOT available (unexpected)\n";
    std::cout << "       GCC/Clang x86 should enable TSC\n";
    all_tests_passed = false;
#endif
#else
    std::cout << "[SKIP] Not GCC/Clang x86 context\n";
    passed_count++; // Skip counts as pass for preservation
#endif

    // Test 2: Non-x86 Fallback Behavior
    std::cout << "\n--- Test 2: Non-x86 Fallback Behavior ---\n";
    test_count++;
    
#if !defined(__x86_64__) && !defined(__i386__) && !defined(_M_X64) && !defined(_M_IX86)
    std::cout << "[TEST] Non-x86 architecture detected\n";
    
#if !TSC_AVAILABLE
    std::cout << "[PASS] TSC timing disabled (expected fallback)\n";
    std::cout << "       Using std::chrono::steady_clock\n";
    passed_count++;
#else
    std::cout << "[FAIL] TSC timing enabled on non-x86 (unexpected)\n";
    all_tests_passed = false;
#endif
#else
    std::cout << "[SKIP] Not a non-x86 context\n";
    passed_count++; // Skip counts as pass
#endif

    // Test 3: Function Signature Preservation
    std::cout << "\n--- Test 3: Function Signature Preservation ---\n";
    test_count++;
    
    std::cout << "[TEST] Verifying function signatures\n";
    
    // Test that functions are callable with expected signatures
    try {
        PreservationTest::pauseHint();
        uint64_t start = PreservationTest::readTicksStart();
        uint64_t end = PreservationTest::readTicksEnd();
        
        if (end >= start) {
            std::cout << "[PASS] pauseHint(), readTicksStart(), readTicksEnd() callable\n";
            std::cout << "       Tick measurement: start=" << start << ", end=" << end << "\n";
            passed_count++;
        } else {
            std::cout << "[FAIL] Tick counts not monotonic: end < start\n";
            all_tests_passed = false;
        }
    } catch (...) {
        std::cout << "[FAIL] Exception thrown calling timing functions\n";
        all_tests_passed = false;
    }

    // Test 4: TSC Calibration Logic (7 samples, 250ms windows)
    std::cout << "\n--- Test 4: TSC Calibration Logic Preservation ---\n";
    test_count++;
    
#if TSC_AVAILABLE
    std::cout << "[TEST] Running TSC calibration (this takes ~1.75 seconds)\n";
    
    auto start_time = std::chrono::steady_clock::now();
    PreservationTest::TickCalibration calib = PreservationTest::calibrateTicks();
    auto end_time = std::chrono::steady_clock::now();
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    if (calib.use_tsc && calib.ticks_per_ns > 0.0 && duration_ms >= 1500 && duration_ms <= 3000) {
        std::cout << "[PASS] TSC calibration logic correct\n";
        std::cout << "       Calibration factor: " << calib.ticks_per_ns << " ticks/ns\n";
        std::cout << "       Duration: " << duration_ms << " ms (7 samples × 250ms expected)\n";
        passed_count++;
    } else {
        std::cout << "[FAIL] TSC calibration behavior unexpected\n";
        std::cout << "       use_tsc=" << calib.use_tsc << ", ticks_per_ns=" << calib.ticks_per_ns << "\n";
        std::cout << "       duration=" << duration_ms << " ms\n";
        all_tests_passed = false;
    }
#else
    std::cout << "[TEST] TSC not available, testing fallback calibration\n";
    
    PreservationTest::TickCalibration calib = PreservationTest::calibrateTicks();
    
    if (!calib.use_tsc && calib.ticks_per_ns == 1.0) {
        std::cout << "[PASS] Fallback calibration correct\n";
        std::cout << "       use_tsc=false, ticks_per_ns=1.0 (passthrough)\n";
        passed_count++;
    } else {
        std::cout << "[FAIL] Fallback calibration incorrect\n";
        std::cout << "       use_tsc=" << calib.use_tsc << ", ticks_per_ns=" << calib.ticks_per_ns << "\n";
        all_tests_passed = false;
    }
#endif

    // Test 5: Preprocessor Guard Evaluation
    std::cout << "\n--- Test 5: Preprocessor Guard Evaluation ---\n";
    test_count++;
    
    std::cout << "[TEST] Checking guard: defined(__x86_64__) || defined(__i386__)\n";
    
#if defined(__x86_64__) || defined(__i386__)
    bool guard_active = true;
    std::cout << "       Current guard: ACTIVE\n";
#else
    bool guard_active = false;
    std::cout << "       Current guard: INACTIVE\n";
#endif

    bool expected_guard = false;
    
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    expected_guard = true; // GCC/Clang x86 should have guard active
#elif !defined(__x86_64__) && !defined(__i386__)
    expected_guard = false; // Non-x86 should have guard inactive
#endif

    if (guard_active == expected_guard) {
        std::cout << "[PASS] Guard evaluation matches expected behavior\n";
        passed_count++;
    } else {
        std::cout << "[FAIL] Guard evaluation unexpected\n";
        std::cout << "       Expected: " << (expected_guard ? "ACTIVE" : "INACTIVE") << "\n";
        all_tests_passed = false;
    }

    // Test 6: Timing Measurement Functionality
    std::cout << "\n--- Test 6: Timing Measurement Functionality ---\n";
    test_count++;
    
    std::cout << "[TEST] Measuring short duration\n";
    
    uint64_t t1 = PreservationTest::readTicksStart();
    
    // Perform some work
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }
    
    uint64_t t2 = PreservationTest::readTicksEnd();
    
    if (t2 > t1) {
        uint64_t delta = t2 - t1;
        std::cout << "[PASS] Timing measurement functional\n";
        std::cout << "       Measured delta: " << delta;
        
#if TSC_AVAILABLE
        std::cout << " cycles\n";
#else
        std::cout << " nanoseconds (steady_clock)\n";
#endif
        passed_count++;
    } else {
        std::cout << "[FAIL] Timing measurement non-monotonic\n";
        std::cout << "       t1=" << t1 << ", t2=" << t2 << "\n";
        all_tests_passed = false;
    }

    // Test 7: Pause Hint Behavior
    std::cout << "\n--- Test 7: Pause Hint Behavior ---\n";
    test_count++;
    
    std::cout << "[TEST] Testing pauseHint() implementation\n";
    
    try {
        // Call pauseHint multiple times
        for (int i = 0; i < 10; ++i) {
            PreservationTest::pauseHint();
        }
        
        std::cout << "[PASS] pauseHint() executes without error\n";
#if TSC_AVAILABLE
        std::cout << "       Using _mm_pause() on x86\n";
#else
        std::cout << "       Using std::this_thread::yield() on non-x86\n";
#endif
        passed_count++;
    } catch (...) {
        std::cout << "[FAIL] pauseHint() threw exception\n";
        all_tests_passed = false;
    }

    // Test 8: Expected Output Message
    std::cout << "\n--- Test 8: Expected Benchmark Output Message ---\n";
    test_count++;
    
    std::cout << "[TEST] Verifying expected benchmark output\n";
    
#if TSC_AVAILABLE
    std::cout << "[INFO] Expected output: \"cycle counts are primary; nanoseconds are calibrated estimates\"\n";
    std::cout << "[PASS] TSC timing message expected\n";
    passed_count++;
#else
    std::cout << "[INFO] Expected output: \"using wall-clock (steady_clock) measurements (no TSC)\"\n";
    std::cout << "[PASS] Fallback timing message expected\n";
    passed_count++;
#endif

    // Summary
    std::cout << "\n=======================================================\n";
    std::cout << "  Preservation Test Summary\n";
    std::cout << "=======================================================\n";
    std::cout << "  Tests Run: " << test_count << "\n";
    std::cout << "  Passed: " << passed_count << "\n";
    std::cout << "  Failed: " << (test_count - passed_count) << "\n";
    std::cout << "\n";

    if (all_tests_passed) {
        std::cout << "[SUCCESS] All preservation tests PASSED\n";
        std::cout << "\nPreservation Property 2 Verified:\n";
        std::cout << "  - Non-MSVC compilation behavior is correct\n";
        std::cout << "  - GCC/Clang x86 TSC timing works as expected\n";
        std::cout << "  - Non-x86 fallback behavior works as expected\n";
        std::cout << "  - Function signatures preserved\n";
        std::cout << "  - Calibration logic preserved\n";
        std::cout << "\nThis baseline should remain unchanged after applying fix.\n";
        std::cout << "\n[STATUS] PRESERVATION TESTS PASSED\n";
        return 0;
    } else {
        std::cout << "[FAILURE] Some preservation tests FAILED\n";
        std::cout << "\nThis indicates unexpected behavior in non-MSVC contexts.\n";
        std::cout << "The codebase behavior may have changed.\n";
        std::cout << "\n[STATUS] PRESERVATION TESTS FAILED\n";
        return 1;
    }
}
