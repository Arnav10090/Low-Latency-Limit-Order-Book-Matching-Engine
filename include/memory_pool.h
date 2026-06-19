#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>

namespace ome {

template <typename T, std::size_t Capacity>
class MemoryPool {
public:
    MemoryPool() : head_(0) {
        static_assert(Capacity > 0, "MemoryPool capacity must be greater than zero");
        for (std::size_t i = 0; i + 1 < Capacity; ++i) {
            free_indices_[i] = i + 1;
        }
        free_indices_[Capacity - 1] = Capacity;
    }

    // Hot-path friendly allocation used by the engine thread to avoid throwing on exhaustion.
    T* tryAllocate() noexcept {
        if (head_ == Capacity) {
            return nullptr;
        }

        const std::size_t free_index = head_;
        head_ = free_indices_[free_index];
        return &storage_[free_index];
    }

    T* allocate() {
        T* slot = tryAllocate();
        if (slot == nullptr) {
            throw std::runtime_error("MemoryPool exhausted");
        }
        return slot;
    }

    void deallocate(T* ptr) {
        if (ptr < storage_.data() || ptr >= storage_.data() + Capacity) {
            throw std::invalid_argument("Pointer does not belong to this MemoryPool");
        }

        const std::size_t index = static_cast<std::size_t>(ptr - storage_.data());
        free_indices_[index] = head_;
        head_ = index;
    }

    std::size_t available() const {
        std::size_t count = 0;
        std::size_t index = head_;
        while (index != Capacity) {
            ++count;
            index = free_indices_[index];
        }
        return count;
    }

    static constexpr std::size_t capacity() {
        return Capacity;
    }

private:
    std::size_t head_;
    std::array<T, Capacity> storage_;
    std::array<std::size_t, Capacity> free_indices_;
};

} // namespace ome
