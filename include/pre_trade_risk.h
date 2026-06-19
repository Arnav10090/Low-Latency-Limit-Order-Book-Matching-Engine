#pragma once
#include <cmath>
#include <unordered_map>

#include "types.h"
#include "timekeeper.h"

namespace ome {

class PreTradeRiskCheck {
public:
    struct ValidationDecision {
        bool accepted = false;
        Nanos decision_time = 0;
        int64_t tokens_after_consume = 0; // scaled integer tokens
        int64_t exposure_delta = 0;
        bool erase_order = false;
        bool upsert_order = false;
        Side tracked_side = Side::BUY;
        Quantity tracked_quantity = 0;
    };

        PreTradeRiskCheck(Quantity max_order_size, int64_t max_position, double max_rate_per_sec)
                : max_order_size_(max_order_size),
                    max_position_(max_position),
                    // Use fixed-point integer tokens to avoid floating-point during validate()
                    rate_limit_tokens_scaled_(static_cast<int64_t>(max_rate_per_sec * TOKEN_SCALE + 0.5)),
                    max_tokens_scaled_(static_cast<int64_t>(max_rate_per_sec * TOKEN_SCALE + 0.5)),
                    tokens_per_ns_scaled_(max_tokens_scaled_ / 1000000000LL),
                    last_check_time_(TimeKeeper::now_ns()) {}

    ValidationDecision validate(const OrderRequest& req) const {
        ValidationDecision decision;

        const Nanos now = TimeKeeper::now_ns();
        const Nanos elapsed_ns = now - last_check_time_;
        const int64_t added_scaled = static_cast<int64_t>(elapsed_ns) * tokens_per_ns_scaled_;
        int64_t available_scaled = rate_limit_tokens_scaled_ + added_scaled;
        if (available_scaled > max_tokens_scaled_) available_scaled = max_tokens_scaled_;

        if (available_scaled < TOKEN_SCALE) {
            return decision;
        }
        decision.decision_time = now;
        decision.tokens_after_consume = available_scaled - TOKEN_SCALE;

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
        rate_limit_tokens_scaled_ = decision.tokens_after_consume;
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
    // Fixed-point tokens: TOKEN_SCALE represents 1.0 token.
    static constexpr int64_t TOKEN_SCALE = 1000; // 1 token == 1000 scaled units
    int64_t rate_limit_tokens_scaled_ = 0;
    int64_t max_tokens_scaled_ = 0;
    // tokens gained per nanosecond in scaled units
    int64_t tokens_per_ns_scaled_ = 0;
    Nanos last_check_time_ = 0;
    int64_t current_position_ = 0;
    std::unordered_map<OrderId, TrackedOrder> tracked_orders_;
};

} // namespace ome
