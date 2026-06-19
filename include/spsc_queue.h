#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

#include "types.h"

namespace ome {

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    // "Lock-free" is part of the contract for this queue on the target platform.
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "SPSCQueue requires lock-free size_t atomics on the target architecture");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    bool push(const T& item) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        const T item = buffer_[head];
        head_.store((head + 1) & mask_, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;

    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_;
    std::array<T, Capacity> buffer_;
};

} // namespace ome
