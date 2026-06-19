#include "order_book.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace ome {

OrderBook::OrderBook(const std::string& symbol)
    : symbol_(symbol) {}

std::vector<Trade> OrderBook::addOrder(Order* order) {
    ++orders_processed_;
    return reprocessModifiedOrder(order);
}

bool OrderBook::cancelOrder(OrderId id) {
    ++orders_processed_;

    const auto index_it = order_index_.find(id);
        if (index_it == order_index_.end()) {
            return false;
        }

        Order* order = index_it->second;
        if (order == nullptr) {
            order_index_.erase(index_it);
            return false;
        }

        const Side side = order->side;
        const Price price = order->price;

        if (side == Side::BUY) {
            bids_[price].total_quantity -= order->remaining;
        } else {
            asks_[price].total_quantity -= order->remaining;
        }

        order->status = OrderStatus::CANCELLED;
        eraseOrderFromLevel(side, price, order);
        order_index_.erase(index_it);
        pool_.deallocate(order);
        return true;
}

std::vector<Trade> OrderBook::modifyOrder(OrderId id, Price new_price, Quantity new_qty) {
    ++orders_processed_;

    const auto index_it = order_index_.find(id);
        if (index_it == order_index_.end()) {
            return {};
        }

        Order* order = index_it->second;
        if (order == nullptr) {
            order_index_.erase(index_it);
            return {};
        }

        const Side side = order->side;
        const Price current_price = order->price;

    if (new_qty == 0U) {
        if (side == Side::BUY) {
            bids_[current_price].total_quantity -= order->remaining;
        } else {
            asks_[current_price].total_quantity -= order->remaining;
        }
        order->status = OrderStatus::CANCELLED;
        eraseOrderFromLevel(side, current_price, order);
        order_index_.erase(index_it);
        pool_.deallocate(order);
        return {};
    }

    const Quantity filled_qty = order->quantity - order->remaining;

    if (new_price != current_price) {
        if (side == Side::BUY) {
            bids_[current_price].total_quantity -= order->remaining;
        } else {
            asks_[current_price].total_quantity -= order->remaining;
        }

        eraseOrderFromLevel(side, current_price, order);
        order_index_.erase(index_it);

        order->price = new_price;
        order->quantity = filled_qty + new_qty;
        order->remaining = new_qty;
        order->timestamp = Order::now_ns();
        order->status = OrderStatus::OPEN;
        return reprocessModifiedOrder(order);
    }

    if (new_qty < order->remaining) {
        const Quantity delta = order->remaining - new_qty;
        if (side == Side::BUY) {
            bids_[current_price].total_quantity -= delta;
        } else {
            asks_[current_price].total_quantity -= delta;
        }

        order->quantity = filled_qty + new_qty;
        order->remaining = new_qty;
        updateRestingStatus(order);
        return {};
    }

    if (new_qty > order->remaining) {
        const Quantity delta = new_qty - order->remaining;
        if (side == Side::BUY) {
            auto& level = bids_[current_price];
            level.total_quantity += delta;
            level.erase(order);
            level.push_back(order);
        } else {
            auto& level = asks_[current_price];
            level.total_quantity += delta;
            level.erase(order);
            level.push_back(order);
        }

        order->quantity = filled_qty + new_qty;
        order->remaining = new_qty;
        order->timestamp = Order::now_ns();
        updateRestingStatus(order);
    }

    return {};
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::size_t OrderBook::bidDepth() const {
    return bids_.size();
}

std::size_t OrderBook::askDepth() const {
    return asks_.size();
}

Quantity OrderBook::totalBidQuantity() const {
    Quantity total = 0;
    for (const auto& entry : bids_) {
        total += entry.second.total_quantity;
    }
    return total;
}

Quantity OrderBook::totalAskQuantity() const {
    Quantity total = 0;
    for (const auto& entry : asks_) {
        total += entry.second.total_quantity;
    }
    return total;
}

void OrderBook::printBook(int levels) const {
    std::cout << "--- Order Book (" << symbol_ << ") ---\n";

    std::vector<std::pair<Price, const PriceLevel*>> ask_levels;
    int count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < levels; ++it, ++count) {
        ask_levels.push_back({it->first, &it->second});
    }

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

std::vector<Trade> OrderBook::matchAgainstAsks(Order* incoming) {
    std::vector<Trade> trades;

    while (incoming->remaining > 0 && !asks_.empty()) {
        auto best_it = asks_.begin();
        const Price best_ask_price = best_it->first;

        if (incoming->type == OrderType::LIMIT && best_ask_price > incoming->price) {
            break;
        }

        PriceLevel& level = best_it->second;
        Order* resting = level.front();
        const Quantity exec_qty = std::min(incoming->remaining, resting->remaining);
        const Price exec_price = resting->price;

        incoming->remaining -= exec_qty;
        resting->remaining -= exec_qty;
        level.total_quantity -= exec_qty;

        trades.push_back(makeTrade(incoming, resting, exec_price, exec_qty));
        ++trades_executed_;

        if (resting->remaining == 0U) {
            resting->status = OrderStatus::FILLED;
            level.pop_front();
            order_index_.erase(resting->id);
            pool_.deallocate(resting);
        } else {
            updateRestingStatus(resting);
        }

        removePriceLevelIfEmpty(Side::SELL, best_ask_price);
    }

    return trades;
}

std::vector<Trade> OrderBook::matchAgainstBids(Order* incoming) {
    std::vector<Trade> trades;

    while (incoming->remaining > 0 && !bids_.empty()) {
        auto best_it = bids_.begin();
        const Price best_bid_price = best_it->first;

        if (incoming->type == OrderType::LIMIT && best_bid_price < incoming->price) {
            break;
        }

        PriceLevel& level = best_it->second;
        Order* resting = level.front();
        const Quantity exec_qty = std::min(incoming->remaining, resting->remaining);
        const Price exec_price = resting->price;

        incoming->remaining -= exec_qty;
        resting->remaining -= exec_qty;
        level.total_quantity -= exec_qty;

        trades.push_back(makeTrade(resting, incoming, exec_price, exec_qty));
        ++trades_executed_;

        if (resting->remaining == 0U) {
            resting->status = OrderStatus::FILLED;
            level.pop_front();
            order_index_.erase(resting->id);
            pool_.deallocate(resting);
        } else {
            updateRestingStatus(resting);
        }

        removePriceLevelIfEmpty(Side::BUY, best_bid_price);
    }

    return trades;
}

void OrderBook::restInBook(Order* order) {
    updateRestingStatus(order);

    if (order->side == Side::BUY) {
        auto& level = bids_[order->price];
        level.push_back(order);
        level.total_quantity += order->remaining;
    } else {
        auto& level = asks_[order->price];
        level.push_back(order);
        level.total_quantity += order->remaining;
    }

    order_index_[order->id] = order;
}

void OrderBook::removePriceLevelIfEmpty(Side side, Price price) {
    if (side == Side::BUY) {
        auto it = bids_.find(price);
        if (it != bids_.end() && it->second.empty()) {
            bids_.erase(it);
        }
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end() && it->second.empty()) {
            asks_.erase(it);
        }
    }
}

Order* OrderBook::findOrder(OrderId id) {
    const auto it = order_index_.find(id);
    if (it == order_index_.end()) return nullptr;
    return it->second;
}

void OrderBook::eraseOrderFromLevel(Side side, Price price, Order* order) {
    if (order == nullptr) return;
    if (side == Side::BUY) {
        auto level_it = bids_.find(price);
        if (level_it == bids_.end()) {
            return;
        }
        level_it->second.erase(order);
    } else {
        auto level_it = asks_.find(price);
        if (level_it == asks_.end()) {
            return;
        }
        level_it->second.erase(order);
    }

    removePriceLevelIfEmpty(side, price);
}

void OrderBook::updateRestingStatus(Order* order) {
    order->status = order->remaining < order->quantity ? OrderStatus::PARTIAL : OrderStatus::OPEN;
}

std::vector<Trade> OrderBook::reprocessModifiedOrder(Order* order) {
    std::vector<Trade> trades;

    if (order->type == OrderType::MARKET) {
        if (order->side == Side::BUY) {
            trades = matchAgainstAsks(order);
        } else {
            trades = matchAgainstBids(order);
        }
        order->status = order->remaining == 0U ? OrderStatus::FILLED : OrderStatus::PARTIAL;
        pool_.deallocate(order);
        return trades;
    }

    if (order->side == Side::BUY) {
        trades = matchAgainstAsks(order);
    } else {
        trades = matchAgainstBids(order);
    }

    if (order->remaining > 0U) {
        restInBook(order);
    } else {
        order->status = OrderStatus::FILLED;
        pool_.deallocate(order);
    }

    return trades;
}

Trade OrderBook::makeTrade(Order* buy, Order* sell, Price exec_price, Quantity qty) {
    Trade trade{};
    trade.buy_order_id = buy->id;
    trade.sell_order_id = sell->id;
    trade.execution_price = exec_price;
    trade.quantity = qty;
    trade.timestamp = Order::now_ns();
    return trade;
}

} // namespace ome
