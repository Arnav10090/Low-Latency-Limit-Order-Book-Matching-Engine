#pragma once
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include "order_book.h"
#include "pre_trade_risk.h"
#include "spsc_queue.h"

namespace ome {

static constexpr std::size_t QUEUE_CAPACITY = 65536;
static constexpr std::size_t TRADE_LOG_CAPACITY = 131072;

struct alignas(CACHE_LINE_SIZE) CompletionSignal {
    std::atomic<RequestId> value{0};
    char padding[CACHE_LINE_SIZE - sizeof(std::atomic<RequestId>)]{};
};

static_assert(std::atomic<RequestId>::is_always_lock_free,
              "Completion signaling requires lock-free RequestId atomics on the target architecture");
static_assert(sizeof(CompletionSignal) == CACHE_LINE_SIZE,
              "CompletionSignal must occupy exactly one cache line");

class MatchingEngine {
public:
    explicit MatchingEngine(const std::string& symbol,
                            Quantity max_order_size = 10'000,
                            int64_t max_position = 100'000,
                            double max_rate_per_sec = 50'000.0);
    ~MatchingEngine();

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    bool submitRequest(const OrderRequest& req);

    void start();
    void stop();
    void setAffinity(int core_id);

    RequestId lastProcessedRequestId() const {
        return completion_signal_.value.load(std::memory_order_acquire);
    }

    const OrderBook& book() const { return book_; }
    const std::vector<Trade>& trades() const { return trades_; }

private:
    PreTradeRiskCheck risk_check_;
    SPSCQueue<OrderRequest, QUEUE_CAPACITY> queue_;
    OrderBook book_;
    // Retain only a bounded trade history so the demo/benchmark path does not grow without limit.
    std::vector<Trade> trades_;
    std::size_t dropped_trade_count_ = 0;
    std::size_t allocation_failures_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> accepting_requests_{false};
    std::atomic<int> requested_affinity_core_{-1};
    CompletionSignal completion_signal_;
    std::thread engine_thread_;

    void engineLoop();
    void processRequest(const OrderRequest& req);
    void appendTrades(const std::vector<Trade>& new_trades);
};

} // namespace ome
