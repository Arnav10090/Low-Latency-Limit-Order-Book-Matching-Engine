cmake_minimum_required(VERSION 3.16)
project(OrderMatchingEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release flags — critical for latency numbers
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

include_directories(include)

# Main binary (interactive demo)
add_executable(matching_engine
    src/main.cpp
    src/order_book.cpp
    src/matching_engine.cpp
)
target_compile_options(matching_engine PRIVATE -Wall -Wextra)

# Benchmark binary
add_executable(benchmark
    benchmark/bench.cpp
    src/order_book.cpp
    src/matching_engine.cpp
)
target_compile_options(benchmark PRIVATE -O3 -march=native)

# Build with: mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4
