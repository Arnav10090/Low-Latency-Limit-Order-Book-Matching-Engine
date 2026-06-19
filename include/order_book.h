#pragma once
#include "types.h"
#include "order.h"
#include "trade.h"
#include "memory_pool.h"
#include <map>
#include <deque>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <string>

namespace ome {

// Size of the pre-allocated order pool
static constexpr std::size_t POOL_SIZE = 1'048'576; // 1M orders

class OrderBook {
public:
    explicit OrderBook(const std::string& symbol);

    // Add a limit or market order. Returns generated trades (if any).
    std::vector<Trade> addOrder(Order* order);

    // Cancel an open order by ID. Returns true if found and cancelled.
    bool cancelOrder(OrderId id);

    // Getters for book state
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::size_t          bidDepth() const;  // number of distinct bid price levels
    std::size_t          askDepth() const;
    Quantity             totalBidQuantity() const;
    Quantity             totalAskQuantity() const;

    // Stats
    uint64_t totalOrdersProcessed() const { return orders_processed_; }
    uint64_t totalTradesExecuted()  const { return trades_executed_; }

    // Pool accessor (used by MatchingEngine to allocate orders)
    MemoryPool<Order, POOL_SIZE>& pool() { return pool_; }
    const MemoryPool<Order, POOL_SIZE>& pool() const { return pool_; }

    // Number of live orders resting in the book
    std::size_t ordersInBook() const { return order_index_.size(); }

    // Print top N levels of bid/ask book to stdout
    void printBook(int levels = 5) const;

private:
    // A price level is a FIFO queue of orders at a single price
    struct PriceLevel {
        std::deque<Order*> orders;
        Quantity           total_quantity = 0;
    };

    // Bids: descending order (highest price = best bid = begin())
    std::map<Price, PriceLevel, std::greater<Price>> bids_;

    // Asks: ascending order (lowest price = best ask = begin())
    std::map<Price, PriceLevel>                      asks_;

    // O(1) lookup for cancellation: order_id -> {side, price}
    std::unordered_map<OrderId, std::pair<Side, Price>> order_index_;

    std::string symbol_;
    uint64_t    orders_processed_ = 0;
    uint64_t    trades_executed_  = 0;

    MemoryPool<Order, POOL_SIZE> pool_;

    // Internal matching logic
    std::vector<Trade> matchAgainstBids(Order* incoming);  // called for SELL orders
    std::vector<Trade> matchAgainstAsks(Order* incoming);  // called for BUY orders
    void               restInBook(Order* order);            // add unmatched remainder
    void               removePriceLevelIfEmpty(Side side, Price price);

    Trade makeTrade(Order* buy, Order* sell, Price exec_price, Quantity qty);
};

} // namespace ome
