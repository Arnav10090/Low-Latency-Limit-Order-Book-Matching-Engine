// Simplified test to verify TSC detection behavior
// **Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5**
//
// This test checks if the TSC timing code path would be taken
// based on the current preprocessor guards.

#include <iostream>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

int main() {
    std::cout << "TSC Detection Test\n";
    std::cout << "==================\n\n";

    // Check which macros are defined
    std::cout << "Architecture Macros:\n";
#if defined(_M_X64)
    std::cout << "  _M_X64: DEFINED (MSVC 64-bit)\n";
#else
    std::cout << "  _M_X64: undefined\n";
#endif

#if defined(_M_IX86)
    std::cout << "  _M_IX86: DEFINED (MSVC 32-bit)\n";
#else
    std::cout << "  _M_IX86: undefined\n";
#endif

#if defined(__x86_64__)
    std::cout << "  __x86_64__: DEFINED (GCC/Clang 64-bit)\n";
#else
    std::cout << "  __x86_64__: undefined\n";
#endif

#if defined(__i386__)
    std::cout << "  __i386__: DEFINED (GCC/Clang 32-bit)\n";
#else
    std::cout << "  __i386__: undefined\n";
#endif

    std::cout << "\nTSC Code Path Status:\n";

#if defined(__x86_64__) || defined(__i386__)
    std::cout << "  Current Guard: ACTIVE\n";
    std::cout << "  TSC Timing: ENABLED\n";
    std::cout << "  Expected Output: \"cycle counts are primary; nanoseconds are calibrated estimates\"\n";
    std::cout << "\n[RESULT] TSC timing is available\n";
    return 0;
#else
    std::cout << "  Current Guard: INACTIVE\n";
    std::cout << "  TSC Timing: DISABLED (fallback to std::chrono)\n";
    std::cout << "  Expected Output: \"using wall-clock (steady_clock) measurements (no TSC)\"\n";
    std::cout << "\n[RESULT] Bug detected - TSC timing is NOT available on MSVC x86/x64\n";
    return 1;
#endif
}
