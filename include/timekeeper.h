#pragma once

#include <atomic>
#include <chrono>

#include "types.h"

namespace ome {

// TimeKeeper provides a cached nanosecond timestamp that can be refreshed
// by the engine thread at a controlled cadence. This avoids calling
// std::chrono::steady_clock::now() in many hot-path locations.
struct TimeKeeper {
    static inline void refresh() noexcept {
        const Nanos ns = static_cast<Nanos>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        cached_ns_.store(ns, std::memory_order_release);
    }

    static inline Nanos now_ns() noexcept {
        return cached_ns_.load(std::memory_order_acquire);
    }

private:
    static inline std::atomic<Nanos> cached_ns_{static_cast<Nanos>(
        std::chrono::steady_clock::now().time_since_epoch().count())};
};

} // namespace ome
