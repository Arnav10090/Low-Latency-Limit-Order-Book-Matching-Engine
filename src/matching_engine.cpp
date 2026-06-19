#include "matching_engine.h"

namespace ome {

MatchingEngine::MatchingEngine(const std::string& symbol)
    : book_(symbol) {}

bool MatchingEngine::submit(Side side, OrderType type, Price price, Quantity qty) {
    Order order = buildOrder(side, type, price, qty);
    return queue_.push(order);  // lock-free enqueue
}

bool MatchingEngine::cancel(OrderId id) {
    // Build a cancel pseudo-order with the target ID
    Order cancel_order{};
    cancel_order.id = id;
    cancel_order.type = OrderType::CANCEL;
    cancel_order.side = Side::BUY;  // side is irrelevant for cancel
    cancel_order.price = 0;
    cancel_order.quantity = 0;
    cancel_order.remaining = 0;
    cancel_order.timestamp = 0;
    cancel_order.status = OrderStatus::OPEN;
    return queue_.push(cancel_order);
}

int MatchingEngine::processAll() {
    int count = 0;
    while (true) {
        auto maybe_order = queue_.pop();
        if (!maybe_order) break;

        // Allocate from pool and copy
        Order* slot = book_.pool().allocate();
        *slot = *maybe_order;  // copy the order data into pool slot
        slot->timestamp = Order::now_ns();

        if (slot->type == OrderType::CANCEL) {
            book_.cancelOrder(slot->id);
            book_.pool().deallocate(slot);
        } else {
            auto new_trades = book_.addOrder(slot);
            trades_.insert(trades_.end(), new_trades.begin(), new_trades.end());
        }
        ++count;
    }

    return count;
}

Order MatchingEngine::buildOrder(Side side, OrderType type, Price price, Quantity qty) {
    Order order{};
    order.id = next_id_++;
    order.side = side;
    order.type = type;
    order.price = price;
    order.quantity = qty;
    order.remaining = qty;
    order.timestamp = 0;  // will be set in processAll()
    order.status = OrderStatus::OPEN;
    return order;
}

} // namespace ome
