#pragma once
#include <chrono>
#include <cmath>
#include <unordered_map>

#include "types.h"

namespace ome {

class PreTradeRiskCheck {
public:
    struct ValidationDecision {
        bool accepted = false;
        std::chrono::time_point<std::chrono::steady_clock> decision_time{};
        double tokens_after_consume = 0.0;
        int64_t exposure_delta = 0;
        bool erase_order = false;
        bool upsert_order = false;
        Side tracked_side = Side::BUY;
        Quantity tracked_quantity = 0;
    };

    PreTradeRiskCheck(Quantity max_order_size, int64_t max_position, double max_rate_per_sec)
        : max_order_size_(max_order_size),
          max_position_(max_position),
          rate_limit_tokens_(max_rate_per_sec),
          max_tokens_(max_rate_per_sec),
          last_check_time_(std::chrono::steady_clock::now()) {}

    ValidationDecision validate(const OrderRequest& req) const {
        ValidationDecision decision;

        const auto now = std::chrono::steady_clock::now();
        const double elapsed_sec = std::chrono::duration<double>(now - last_check_time_).count();
        double available_tokens = rate_limit_tokens_ + (elapsed_sec * max_tokens_);
        if (available_tokens > max_tokens_) {
            available_tokens = max_tokens_;
        }

        if (available_tokens < 1.0) {
            return decision;
        }
        decision.decision_time = now;
        decision.tokens_after_consume = available_tokens - 1.0;

        if (req.action == ActionType::CANCEL) {
            decision.accepted = true;
            decision.erase_order = true;
            return decision;
        }

        if (req.quantity > max_order_size_) {
            return decision;
        }

        if (req.action == ActionType::NEW) {
            decision.exposure_delta = signedQuantity(req.side, req.quantity);
            if (std::abs(current_position_ + decision.exposure_delta) > max_position_) {
                return ValidationDecision{};
            }

            decision.accepted = true;
            decision.upsert_order = true;
            decision.tracked_side = req.side;
            decision.tracked_quantity = req.quantity;
            return decision;
        }

        if (req.action == ActionType::MODIFY) {
            const auto tracked_it = tracked_orders_.find(req.id);
            if (tracked_it == tracked_orders_.end()) {
                decision.accepted = true;
                return decision;
            }

            const TrackedOrder& tracked = tracked_it->second;
            decision.tracked_side = tracked.side;

            if (req.quantity == 0U) {
                decision.accepted = true;
                decision.erase_order = true;
                return decision;
            }

            if (req.quantity > tracked.quantity) {
                decision.exposure_delta = signedQuantity(
                    tracked.side, static_cast<Quantity>(req.quantity - tracked.quantity));
                if (std::abs(current_position_ + decision.exposure_delta) > max_position_) {
                    return ValidationDecision{};
                }
            }

            decision.accepted = true;
            decision.upsert_order = true;
            decision.tracked_quantity = req.quantity;
            return decision;
        }

        return decision;
    }

    void commit(const OrderRequest& req, const ValidationDecision& decision) {
        if (!decision.accepted) {
            return;
        }

        last_check_time_ = decision.decision_time;
        rate_limit_tokens_ = decision.tokens_after_consume;
        current_position_ += decision.exposure_delta;

        if (decision.erase_order) {
            tracked_orders_.erase(req.id);
        }

        if (decision.upsert_order) {
            tracked_orders_[req.id] = TrackedOrder{decision.tracked_side, decision.tracked_quantity};
        }
    }

private:
    struct TrackedOrder {
        Side side;
        Quantity quantity;
    };

    static int64_t signedQuantity(Side side, Quantity quantity) {
        const int64_t signed_qty = static_cast<int64_t>(quantity);
        return side == Side::BUY ? signed_qty : -signed_qty;
    }

    Quantity max_order_size_;
    int64_t max_position_;
    double rate_limit_tokens_;
    double max_tokens_;
    std::chrono::time_point<std::chrono::steady_clock> last_check_time_;
    int64_t current_position_ = 0;
    std::unordered_map<OrderId, TrackedOrder> tracked_orders_;
};

} // namespace ome
