#pragma once
#include <cstdint>

namespace ome {

enum class Side : uint8_t {
    BUY  = 0,
    SELL = 1
};

enum class OrderType : uint8_t {
    LIMIT  = 0,
    MARKET = 1,
    CANCEL = 2
};

enum class OrderStatus : uint8_t {
    OPEN      = 0,
    FILLED    = 1,
    PARTIAL   = 2,
    CANCELLED = 3,
    REJECTED  = 4
};

// Prices stored as integers (price * 100) to avoid floating-point comparison.
// e.g., $150.25 is stored as 15025.
// This is standard practice in HFT systems.
using Price    = int64_t;
using Quantity = uint32_t;
using OrderId  = uint64_t;
using Nanos    = uint64_t;

// Cache line size constant for alignment/padding
static constexpr size_t CACHE_LINE_SIZE = 64;

} // namespace ome
