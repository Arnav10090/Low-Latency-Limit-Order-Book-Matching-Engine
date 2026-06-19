#pragma once
#include <atomic>
#include <vector>
#include <optional>
#include <cstddef>
#include "types.h"

namespace ome {

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2");

public:
    SPSCQueue() : head_(0), tail_(0), buffer_(Capacity) {}

    // Producer side — returns false if queue is full
    bool push(const T& item) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side — returns empty optional if queue is empty
    std::optional<T> pop() {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt; // empty
        }
        T item = buffer_[head];
        head_.store((head + 1) & mask_, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    // Pad head and tail to separate cache lines — prevents false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_;

    // Heap-allocated ring buffer to avoid stack overflow (64K × 64 bytes = 4MB).
    // Using std::vector with pre-sized capacity — single contiguous allocation,
    // no reallocation. Functionally identical to std::array but on the heap.
    std::vector<T> buffer_;
};

} // namespace ome
