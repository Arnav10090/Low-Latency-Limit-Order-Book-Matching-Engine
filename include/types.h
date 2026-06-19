#pragma once
#include <cstddef>
#include <cstdint>

namespace ome {

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

enum class OrderType : uint8_t {
    LIMIT = 0,
    MARKET = 1
};

enum class ActionType : uint8_t {
    NEW = 0,
    MODIFY = 1,
    CANCEL = 2
};

enum class OrderStatus : uint8_t {
    OPEN = 0,
    FILLED = 1,
    PARTIAL = 2,
    CANCELLED = 3,
    REJECTED = 4
};

using Price = int64_t;
using Quantity = uint32_t;
using OrderId = uint64_t;
using RequestId = uint64_t;
using Nanos = uint64_t;

static constexpr std::size_t CACHE_LINE_SIZE = 64;

struct OrderRequest {
    ActionType action;
    RequestId request_id;
    OrderId id;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity;
};

} // namespace ome
