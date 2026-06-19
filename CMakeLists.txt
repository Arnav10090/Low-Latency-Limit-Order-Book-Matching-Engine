cmake_minimum_required(VERSION 3.16)
project(OrderMatchingEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release flags — critical for latency numbers
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
endif()

include_directories(include)

# Main binary (interactive demo)
add_executable(matching_engine
    src/main.cpp
    src/order_book.cpp
    src/engine_thread.cpp
)
if(MSVC)
    target_compile_options(matching_engine PRIVATE /W4)
else()
    target_compile_options(matching_engine PRIVATE -Wall -Wextra)
    target_link_libraries(matching_engine pthread)
endif()

# Benchmark binary
add_executable(benchmark
    benchmark/bench.cpp
    src/order_book.cpp
    src/engine_thread.cpp
)
if(MSVC)
    target_compile_options(benchmark PRIVATE /W4 /O2)
else()
    target_compile_options(benchmark PRIVATE -Wall -Wextra -O3 -march=native)
    target_link_libraries(benchmark pthread)
endif()

# Bug condition exploration test
add_executable(test_msvc_bug_condition
    test_msvc_bug_condition.cpp
)
if(MSVC)
    target_compile_options(test_msvc_bug_condition PRIVATE /W4)
else()
    target_compile_options(test_msvc_bug_condition PRIVATE -Wall -Wextra)
endif()

# TSC detection verification test
add_executable(test_tsc_detection
    test_tsc_detection.cpp
)
if(MSVC)
    target_compile_options(test_tsc_detection PRIVATE /W4)
else()
    target_compile_options(test_tsc_detection PRIVATE -Wall -Wextra)
endif()

# Preservation property tests
add_executable(test_preservation
    test_preservation.cpp
)
if(MSVC)
    target_compile_options(test_preservation PRIVATE /W4)
else()
    target_compile_options(test_preservation PRIVATE -Wall -Wextra)
    target_link_libraries(test_preservation pthread)
endif()

# Fix verification test (verifies TSC is enabled after fix)
add_executable(test_fix_verification
    test_fix_verification.cpp
)
if(MSVC)
    target_compile_options(test_fix_verification PRIVATE /W4)
else()
    target_compile_options(test_fix_verification PRIVATE -Wall -Wextra)
endif()

# Build with: mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4
