#pragma once
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory_pool.h"
#include "order.h"
#include "trade.h"
#include "types.h"

namespace ome {

static constexpr std::size_t POOL_SIZE = 1'048'576; // 1M orders

class OrderBook {
public:
    explicit OrderBook(const std::string& symbol);

    std::vector<Trade> addOrder(Order* order);
    bool cancelOrder(OrderId id);
    std::vector<Trade> modifyOrder(OrderId id, Price new_price, Quantity new_qty);

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::size_t bidDepth() const;
    std::size_t          askDepth() const;
    Quantity             totalBidQuantity() const;
    Quantity             totalAskQuantity() const;

    uint64_t totalOrdersProcessed() const { return orders_processed_; }
    uint64_t totalTradesExecuted()  const { return trades_executed_; }

    MemoryPool<Order, POOL_SIZE>& pool() { return pool_; }
    const MemoryPool<Order, POOL_SIZE>& pool() const { return pool_; }

    std::size_t ordersInBook() const { return order_index_.size(); }

    void printBook(int levels = 5) const;

private:
    struct PriceLevel {
        Order* head = nullptr;
        Order* tail = nullptr;
        Quantity total_quantity = 0;

        inline Order* front() const { return head; }
        inline bool empty() const { return head == nullptr; }

        inline void push_back(Order* o) {
            o->next_in_level = nullptr;
            o->prev_in_level = tail;
            if (tail) tail->next_in_level = o;
            else head = o;
            tail = o;
        }

        inline void pop_front() {
            if (!head) return;
            Order* o = head;
            head = o->next_in_level;
            if (head) head->prev_in_level = nullptr;
            else tail = nullptr;
            o->next_in_level = o->prev_in_level = nullptr;
        }

        inline void erase(Order* o) {
            if (o->prev_in_level) o->prev_in_level->next_in_level = o->next_in_level;
            else head = o->next_in_level;
            if (o->next_in_level) o->next_in_level->prev_in_level = o->prev_in_level;
            else tail = o->prev_in_level;
            o->next_in_level = o->prev_in_level = nullptr;
        }
    };

    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel>                      asks_;
    std::unordered_map<OrderId, Order*>              order_index_;

    std::string symbol_;
    uint64_t    orders_processed_ = 0;
    uint64_t    trades_executed_  = 0;
    MemoryPool<Order, POOL_SIZE> pool_;

    std::vector<Trade> matchAgainstBids(Order* incoming);
    std::vector<Trade> matchAgainstAsks(Order* incoming);
    void restInBook(Order* order);
    void removePriceLevelIfEmpty(Side side, Price price);
    Order* findOrder(OrderId id);
    void eraseOrderFromLevel(Side side, Price price, Order* order);
    void updateRestingStatus(Order* order);
    std::vector<Trade> reprocessModifiedOrder(Order* order);

    Trade makeTrade(Order* buy, Order* sell, Price exec_price, Quantity qty);
};

} // namespace ome
