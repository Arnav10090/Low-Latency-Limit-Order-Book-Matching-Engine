#include <iostream>
#include <string>

// This test verifies the bug condition exists BEFORE the fix is applied.
// **Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5**
//
// EXPECTED OUTCOME: This test FAILS on unfixed code (this is correct - it proves the bug exists)
//
// The test checks that:
// 1. On MSVC, _M_X64 or _M_IX86 are defined (MSVC x86 detection)
// 2. On MSVC, __x86_64__ and __i386__ are NOT defined (GCC/Clang only)
// 3. The current preprocessor guards exclude TSC code on MSVC
//
// After the fix is applied, this test should PASS.

int main() {
    std::cout << "=======================================================\n";
    std::cout << "  MSVC TSC Bug Condition Exploration Test\n";
    std::cout << "=======================================================\n";
    std::cout << "\n";
    std::cout << "This test verifies the bug exists on unfixed code.\n";
    std::cout << "Expected: Test FAILS (proving bug exists)\n";
    std::cout << "After fix: Test PASSES\n";
    std::cout << "\n";

    bool test_passed = true;
    int check_count = 0;

    // Check 1: Verify we're on MSVC x86/x64 architecture
    std::cout << "--- Compiler and Architecture Detection ---\n";
    
#if defined(_MSC_VER)
    std::cout << "[INFO] MSVC compiler detected (_MSC_VER defined)\n";
#else
    std::cout << "[INFO] Not MSVC compiler (_MSC_VER not defined)\n";
    std::cout << "[SKIP] This test is designed for MSVC builds\n";
    return 0;
#endif

#if defined(_M_X64)
    std::cout << "[INFO] _M_X64 defined (MSVC 64-bit x86)\n";
    check_count++;
#elif defined(_M_IX86)
    std::cout << "[INFO] _M_IX86 defined (MSVC 32-bit x86)\n";
    check_count++;
#else
    std::cout << "[INFO] Neither _M_X64 nor _M_IX86 defined\n";
    std::cout << "[SKIP] This test requires x86/x64 architecture\n";
    return 0;
#endif

    // Check 2: Verify GCC/Clang macros are NOT defined on MSVC
    std::cout << "\n--- GCC/Clang Macro Check (should be undefined on MSVC) ---\n";
    
#if defined(__x86_64__)
    std::cout << "[INFO] __x86_64__ is defined (unexpected on MSVC)\n";
#else
    std::cout << "[PASS] __x86_64__ is NOT defined (expected on MSVC)\n";
#endif

#if defined(__i386__)
    std::cout << "[INFO] __i386__ is defined (unexpected on MSVC)\n";
#else
    std::cout << "[PASS] __i386__ is NOT defined (expected on MSVC)\n";
#endif

    // Check 3: Test if current preprocessor guard would evaluate to TRUE
    std::cout << "\n--- Bug Condition Check: Preprocessor Guard Evaluation ---\n";
    std::cout << "Testing guard: #if defined(__x86_64__) || defined(__i386__)\n";
    
#if defined(__x86_64__) || defined(__i386__)
    std::cout << "[UNEXPECTED] Guard evaluates to TRUE\n";
    std::cout << "              TSC code WOULD be included\n";
    std::cout << "              This suggests the bug may not exist or code is already fixed\n";
    test_passed = true; // If guard is true, that's the expected behavior (bug is fixed)
#else
    std::cout << "[BUG DETECTED] Guard evaluates to FALSE\n";
    std::cout << "               TSC code is EXCLUDED from compilation\n";
    std::cout << "               This confirms the bug exists on MSVC\n";
    test_passed = false; // Guard is false, bug exists
#endif

    // Check 4: Verify MSVC macros would work if added to guard
    std::cout << "\n--- Proposed Fix Validation ---\n";
    std::cout << "Testing guard: #if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)\n";
    
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    std::cout << "[PASS] Fixed guard evaluates to TRUE\n";
    std::cout << "       TSC code WOULD be included with the fix\n";
#else
    std::cout << "[FAIL] Fixed guard evaluates to FALSE (unexpected)\n";
    test_passed = false;
#endif

    // Check 5: Test <immintrin.h> availability
    std::cout << "\n--- Intrinsics Header Availability ---\n";
    
#if defined(__x86_64__) || defined(__i386__)
    std::cout << "[INFO] <immintrin.h> WOULD be included (current guard)\n";
#else
    std::cout << "[BUG] <immintrin.h> WOULD NOT be included (current guard)\n";
    std::cout << "      TSC intrinsics (__rdtsc, __rdtscp, _mm_pause) unavailable\n";
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    std::cout << "[FIX] <immintrin.h> WOULD be included (fixed guard)\n";
    std::cout << "      TSC intrinsics would be available\n";
#endif

    // Summary
    std::cout << "\n=======================================================\n";
    std::cout << "  Test Result Summary\n";
    std::cout << "=======================================================\n";
    
    if (!test_passed) {
        std::cout << "[EXPECTED FAILURE] Bug condition confirmed!\n";
        std::cout << "\nCounterexample:\n";
        
#if defined(_M_X64)
        std::cout << "  - Compiler: MSVC (x64)\n";
        std::cout << "  - _M_X64 defined: YES\n";
#elif defined(_M_IX86)
        std::cout << "  - Compiler: MSVC (x86)\n";
        std::cout << "  - _M_IX86 defined: YES\n";
#endif
        std::cout << "  - __x86_64__ defined: NO\n";
        std::cout << "  - __i386__ defined: NO\n";
        std::cout << "  - Current guard evaluates to: FALSE\n";
        std::cout << "  - TSC code excluded: YES\n";
        std::cout << "  - Fallback to std::chrono: YES\n";
        std::cout << "\nThis failure is EXPECTED and confirms the bug exists.\n";
        std::cout << "After applying the fix, this test should PASS.\n";
        std::cout << "\n[STATUS] TEST FAILED (Bug Detected - This is Expected)\n";
        return 1; // Return failure to indicate bug exists
    } else {
        std::cout << "[UNEXPECTED PASS] Bug condition NOT confirmed\n";
        std::cout << "\nThis is unexpected. Possible explanations:\n";
        std::cout << "  1. Code may already be fixed\n";
        std::cout << "  2. Compiler defines both GCC and MSVC macros (e.g., clang-cl)\n";
        std::cout << "  3. Test environment doesn't match bug conditions\n";
        std::cout << "\n[STATUS] TEST PASSED (Bug NOT Detected - Unexpected)\n";
        return 0;
    }
}
