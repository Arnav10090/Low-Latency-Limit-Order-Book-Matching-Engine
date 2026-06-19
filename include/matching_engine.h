#pragma once
#include "order_book.h"
#include "spsc_queue.h"
#include <string>
#include <vector>

namespace ome {

// Queue capacity: power of 2
static constexpr std::size_t QUEUE_CAPACITY = 65536;

class MatchingEngine {
public:
    explicit MatchingEngine(const std::string& symbol);

    // Submit a raw order descriptor. Engine allocates from pool and enqueues.
    // Returns false if queue is full.
    bool submit(Side side, OrderType type, Price price, Quantity qty);

    // Cancel a specific order by ID.
    bool cancel(OrderId id);

    // Process all pending orders in the queue. Call in a loop.
    // Returns number of orders processed in this call.
    int  processAll();

    // Accessors
    const std::vector<Trade>& trades()    const { return trades_; }
    const OrderBook&          book()      const { return book_; }
    OrderId                   nextId()    const { return next_id_; }

    // Clear trade log (call between benchmark batches if needed)
    void clearTrades() { trades_.clear(); }

private:
    OrderBook book_;
    SPSCQueue<Order, QUEUE_CAPACITY> queue_;
    std::vector<Trade> trades_;    // accumulates all trades across all processAll() calls
    OrderId next_id_ = 1;

    Order buildOrder(Side side, OrderType type, Price price, Quantity qty);
};

} // namespace ome
