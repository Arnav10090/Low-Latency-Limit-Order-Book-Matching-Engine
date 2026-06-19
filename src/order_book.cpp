#include "order_book.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace ome {

OrderBook::OrderBook(const std::string& symbol)
    : symbol_(symbol) {}

std::vector<Trade> OrderBook::addOrder(Order* order) {
    ++orders_processed_;
    std::vector<Trade> trades;

    if (order->type == OrderType::MARKET) {
        if (order->side == Side::BUY) {
            trades = matchAgainstAsks(order);
        } else {
            trades = matchAgainstBids(order);
        }
        // Discard unfilled remainder of market order (no resting)
        pool_.deallocate(order);
        return trades;
    }

    // LIMIT order
    if (order->side == Side::BUY) {
        trades = matchAgainstAsks(order);
    } else {
        trades = matchAgainstBids(order);
    }

    if (order->remaining > 0) {
        restInBook(order);
    } else {
        // Fully filled — return to pool
        order->status = OrderStatus::FILLED;
        pool_.deallocate(order);
    }

    return trades;
}

bool OrderBook::cancelOrder(OrderId id) {
    ++orders_processed_;
    auto it = order_index_.find(id);
    if (it == order_index_.end()) return false;

    auto [side, price] = it->second;

    if (side == Side::BUY) {
        auto level_it = bids_.find(price);
        if (level_it != bids_.end()) {
            auto& deq = level_it->second.orders;
            for (auto oit = deq.begin(); oit != deq.end(); ++oit) {
                if ((*oit)->id == id) {
                    Order* order = *oit;
                    level_it->second.total_quantity -= order->remaining;
                    order->status = OrderStatus::CANCELLED;
                    deq.erase(oit);
                    pool_.deallocate(order);
                    break;
                }
            }
            removePriceLevelIfEmpty(Side::BUY, price);
        }
    } else {
        auto level_it = asks_.find(price);
        if (level_it != asks_.end()) {
            auto& deq = level_it->second.orders;
            for (auto oit = deq.begin(); oit != deq.end(); ++oit) {
                if ((*oit)->id == id) {
                    Order* order = *oit;
                    level_it->second.total_quantity -= order->remaining;
                    order->status = OrderStatus::CANCELLED;
                    deq.erase(oit);
                    pool_.deallocate(order);
                    break;
                }
            }
            removePriceLevelIfEmpty(Side::SELL, price);
        }
    }

    order_index_.erase(it);
    return true;
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::size_t OrderBook::bidDepth() const { return bids_.size(); }
std::size_t OrderBook::askDepth() const { return asks_.size(); }

Quantity OrderBook::totalBidQuantity() const {
    Quantity total = 0;
    for (const auto& [price, level] : bids_) {
        total += level.total_quantity;
    }
    return total;
}

Quantity OrderBook::totalAskQuantity() const {
    Quantity total = 0;
    for (const auto& [price, level] : asks_) {
        total += level.total_quantity;
    }
    return total;
}

void OrderBook::printBook(int levels) const {
    std::cout << "--- Order Book (" << symbol_ << ") ---\n";

    // Print asks in reverse order (highest first, then lower)
    std::vector<std::pair<Price, const PriceLevel*>> ask_levels;
    int count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < levels; ++it, ++count) {
        ask_levels.push_back({it->first, &it->second});
    }
    // Print from highest to lowest ask
    for (auto rit = ask_levels.rbegin(); rit != ask_levels.rend(); ++rit) {
        std::cout << "ASK  " << std::setw(5) << rit->first
                  << " | " << std::setw(3) << rit->second->total_quantity << " |\n";
    }

    std::cout << "          ----------\n";

    if (bids_.empty()) {
        std::cout << "(empty bids)\n";
    } else {
        count = 0;
        for (auto it = bids_.begin(); it != bids_.end() && count < levels; ++it, ++count) {
            std::cout << "BID  " << std::setw(5) << it->first
                      << " | " << std::setw(3) << it->second.total_quantity << " |\n";
        }
    }
    std::cout << "\n";
}

// --- Private methods ---

std::vector<Trade> OrderBook::matchAgainstAsks(Order* incoming) {
    std::vector<Trade> trades;

    while (incoming->remaining > 0 && !asks_.empty()) {
        auto best_it = asks_.begin();
        Price best_ask_price = best_it->first;

        // For LIMIT orders, stop if best ask exceeds our limit price
        if (incoming->type == OrderType::LIMIT && best_ask_price > incoming->price) {
            break;
        }

        PriceLevel& level = best_it->second;
        Order* resting = level.orders.front();

        Quantity exec_qty = std::min(incoming->remaining, resting->remaining);
        Price exec_price = resting->price; // resting order sets the price (maker pricing)

        incoming->remaining -= exec_qty;
        resting->remaining -= exec_qty;
        level.total_quantity -= exec_qty;

        Trade trade = makeTrade(incoming, resting, exec_price, exec_qty);
        trades.push_back(trade);
        ++trades_executed_;

        if (resting->remaining == 0) {
            resting->status = OrderStatus::FILLED;
            level.orders.pop_front();
            order_index_.erase(resting->id);
            pool_.deallocate(resting);
        }

        removePriceLevelIfEmpty(Side::SELL, best_ask_price);
    }

    return trades;
}

std::vector<Trade> OrderBook::matchAgainstBids(Order* incoming) {
    std::vector<Trade> trades;

    while (incoming->remaining > 0 && !bids_.empty()) {
        auto best_it = bids_.begin();
        Price best_bid_price = best_it->first;

        // For LIMIT orders, stop if best bid is below our limit price
        if (incoming->type == OrderType::LIMIT && best_bid_price < incoming->price) {
            break;
        }

        PriceLevel& level = best_it->second;
        Order* resting = level.orders.front();

        Quantity exec_qty = std::min(incoming->remaining, resting->remaining);
        Price exec_price = resting->price; // resting order sets the price (maker pricing)

        incoming->remaining -= exec_qty;
        resting->remaining -= exec_qty;
        level.total_quantity -= exec_qty;

        Trade trade = makeTrade(resting, incoming, exec_price, exec_qty);
        trades.push_back(trade);
        ++trades_executed_;

        if (resting->remaining == 0) {
            resting->status = OrderStatus::FILLED;
            level.orders.pop_front();
            order_index_.erase(resting->id);
            pool_.deallocate(resting);
        }

        removePriceLevelIfEmpty(Side::BUY, best_bid_price);
    }

    return trades;
}

void OrderBook::restInBook(Order* order) {
    if (order->remaining < order->quantity) {
        order->status = OrderStatus::PARTIAL;
    } else {
        order->status = OrderStatus::OPEN;
    }

    if (order->side == Side::BUY) {
        auto& level = bids_[order->price];
        level.orders.push_back(order);
        level.total_quantity += order->remaining;
    } else {
        auto& level = asks_[order->price];
        level.orders.push_back(order);
        level.total_quantity += order->remaining;
    }

    order_index_[order->id] = {order->side, order->price};
}

void OrderBook::removePriceLevelIfEmpty(Side side, Price price) {
    if (side == Side::BUY) {
        auto it = bids_.find(price);
        if (it != bids_.end() && it->second.orders.empty()) {
            bids_.erase(it);
        }
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end() && it->second.orders.empty()) {
            asks_.erase(it);
        }
    }
}

Trade OrderBook::makeTrade(Order* buy, Order* sell, Price exec_price, Quantity qty) {
    Trade t;
    t.buy_order_id = buy->id;
    t.sell_order_id = sell->id;
    t.execution_price = exec_price;
    t.quantity = qty;
    t.timestamp = Order::now_ns();
    return t;
}

} // namespace ome
