#pragma once
#include "types.h"

namespace ome {

struct Trade {
    OrderId  buy_order_id;
    OrderId  sell_order_id;
    Price    execution_price;   // price at which the trade executed
    Quantity quantity;          // shares exchanged
    Nanos    timestamp;
};

} // namespace ome
