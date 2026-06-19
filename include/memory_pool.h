#pragma once
#include <vector>
#include <stack>
#include <stdexcept>
#include <cstddef>

namespace ome {

template <typename T, std::size_t Capacity>
class MemoryPool {
public:
    MemoryPool() : storage_(Capacity) {
        // Pre-populate free list with pointers to all slots
        for (std::size_t i = 0; i < Capacity; ++i) {
            free_list_.push(&storage_[i]);
        }
    }

    // Allocate one object slot. Throws if pool exhausted.
    T* allocate() {
        if (free_list_.empty()) {
            throw std::runtime_error("MemoryPool exhausted");
        }
        T* slot = free_list_.top();
        free_list_.pop();
        return slot;
    }

    // Return slot to pool. Does NOT call destructor — caller's responsibility.
    void deallocate(T* ptr) {
        free_list_.push(ptr);
    }

    std::size_t available() const { return free_list_.size(); }
    std::size_t capacity()  const { return Capacity; }

private:
    // Heap-allocated storage to avoid 64MB stack overflow.
    // std::vector performs a single contiguous allocation identical to
    // std::array but lives on the heap. The constructor above pre-sizes
    // it to Capacity elements, so no reallocation ever occurs.
    std::vector<T> storage_;
    std::stack<T*> free_list_;
};

} // namespace ome
