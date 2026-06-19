#pragma once
#include "types.h"
#include "timekeeper.h"
#include <chrono>

namespace ome {

struct Order {
    OrderId   id;           // 8 bytes  (offset 0)
    Side      side;         // 1 byte   (offset 8)
    OrderType type;         // 1 byte   (offset 9)
    // 6 bytes compiler padding to align Price (offset 10-15)
    Price     price;        // 8 bytes  (offset 16) — integer price (cents). Ignored for MARKET orders.
    Quantity  quantity;     // 4 bytes  (offset 24) — original quantity
    Quantity  remaining;    // 4 bytes  (offset 28) — unfilled quantity (decremented during matching)
    Nanos     timestamp;    // 8 bytes  (offset 32) — nanoseconds since epoch, set at order creation
    OrderStatus status;     // 1 byte   (offset 40)
    // 7 bytes compiler padding (offset 41-47)
    // Total compiler-padded struct = 48 bytes

    // Intrusive links for price-level ordering. Use the previous padding
    // space to avoid increasing the size of `Order` (two pointers = 16 bytes on x64).
    Order* next_in_level = nullptr;
    Order* prev_in_level = nullptr;

    // Factory helper — delegate to TimeKeeper for a cached nanosecond clock.
    static inline Nanos now_ns() noexcept {
        return TimeKeeper::now_ns();
    }
};

static_assert(sizeof(Order) == 64, "Order must be exactly one cache line");

} // namespace ome
