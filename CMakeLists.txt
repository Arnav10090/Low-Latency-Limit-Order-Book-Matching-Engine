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

# Build with: mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4
