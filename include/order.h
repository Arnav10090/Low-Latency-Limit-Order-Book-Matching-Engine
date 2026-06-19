#pragma once
#include "types.h"
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

    // Explicit padding to fill one 64-byte cache line.
    // 64 - 48 = 16 bytes needed.
    char pad[16];

    // Factory helper
    static Nanos now_ns() {
        return static_cast<Nanos>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
    }
};

static_assert(sizeof(Order) == 64, "Order must be exactly one cache line");

} // namespace ome
