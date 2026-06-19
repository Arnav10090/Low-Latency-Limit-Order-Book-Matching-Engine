#include "engine_thread.h"

#if defined(__linux__)
#include <pthread.h>
#endif

#include <stdexcept>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace ome {

MatchingEngine::MatchingEngine(const std::string& symbol,
                               Quantity max_order_size,
                               int64_t max_position,
                               double max_rate_per_sec)
    : risk_check_(max_order_size, max_position, max_rate_per_sec),
      book_(symbol) {
    trades_.reserve(TRADE_LOG_CAPACITY);
}

MatchingEngine::~MatchingEngine() {
    stop();
}

bool MatchingEngine::submitRequest(const OrderRequest& req) {
    if (!accepting_requests_.load(std::memory_order_acquire)) {
        return false;
    }

    const PreTradeRiskCheck::ValidationDecision decision = risk_check_.validate(req);
    if (!decision.accepted) {
        return false;
    }

    if (!accepting_requests_.load(std::memory_order_acquire)) {
        return false;
    }

    if (!queue_.push(req)) {
        return false;
    }

    risk_check_.commit(req, decision);
    return true;
}

void MatchingEngine::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    accepting_requests_.store(true, std::memory_order_release);
    engine_thread_ = std::thread(&MatchingEngine::engineLoop, this);
}

void MatchingEngine::stop() {
    accepting_requests_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);

    if (engine_thread_.joinable()) {
        engine_thread_.join();
    }
}

void MatchingEngine::setAffinity(int core_id) {
    if (!engine_thread_.joinable()) {
        throw std::runtime_error("Engine thread must be running before setting affinity");
    }

#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    const int rc = pthread_setaffinity_np(engine_thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        throw std::system_error(rc, std::generic_category(), "pthread_setaffinity_np failed");
    }
#elif defined(_WIN32)
    requested_affinity_core_.store(core_id, std::memory_order_release);
#else
    (void)core_id;
    throw std::runtime_error("Thread affinity is not supported on this platform");
#endif
}

void MatchingEngine::engineLoop() {
#if defined(_WIN32)
    int applied_affinity_core = -1;
#endif
    for (;;) {
#if defined(_WIN32)
        const int requested_affinity_core = requested_affinity_core_.load(std::memory_order_acquire);
        if (requested_affinity_core >= 0 && requested_affinity_core != applied_affinity_core) {
            const DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL << requested_affinity_core);
            if (SetThreadAffinityMask(GetCurrentThread(), mask) != 0) {
                applied_affinity_core = requested_affinity_core;
            }
        }
#endif

        auto maybe_request = queue_.pop();
        if (maybe_request.has_value()) {
            processRequest(*maybe_request);
            continue;
        }

        if (!running_.load(std::memory_order_acquire)) {
            break;
        }

#if defined(__x86_64__) || defined(__i386__)
        _mm_pause();
#else
        std::this_thread::yield();
#endif
    }

    while (true) {
        auto maybe_request = queue_.pop();
        if (!maybe_request.has_value()) {
            break;
        }
        processRequest(*maybe_request);
    }
}

void MatchingEngine::processRequest(const OrderRequest& req) {
    if (req.action == ActionType::NEW) {
        Order* order = book_.pool().tryAllocate();
        if (order == nullptr) {
            ++allocation_failures_;
            completion_signal_.value.store(req.request_id, std::memory_order_release);
            return;
        }
        order->id = req.id;
        order->side = req.side;
        order->type = req.type;
        order->price = req.price;
        order->quantity = req.quantity;
        order->remaining = req.quantity;
        order->timestamp = Order::now_ns();
        order->status = OrderStatus::OPEN;

        auto new_trades = book_.addOrder(order);
        appendTrades(new_trades);
    } else if (req.action == ActionType::CANCEL) {
        book_.cancelOrder(req.id);
    } else if (req.action == ActionType::MODIFY) {
        auto new_trades = book_.modifyOrder(req.id, req.price, req.quantity);
        appendTrades(new_trades);
    }

    completion_signal_.value.store(req.request_id, std::memory_order_release);
}

void MatchingEngine::appendTrades(const std::vector<Trade>& new_trades) {
    if (new_trades.empty()) {
        return;
    }

    const std::size_t retained_trade_count = trades_.size();
    if (retained_trade_count >= TRADE_LOG_CAPACITY) {
        dropped_trade_count_ += new_trades.size();
        return;
    }

    const std::size_t remaining_capacity = TRADE_LOG_CAPACITY - retained_trade_count;
    const std::size_t trades_to_retain = (std::min)(remaining_capacity, new_trades.size());
    trades_.insert(trades_.end(), new_trades.begin(), new_trades.begin() + static_cast<std::ptrdiff_t>(trades_to_retain));

    if (trades_to_retain < new_trades.size()) {
        dropped_trade_count_ += new_trades.size() - trades_to_retain;
    }
}

} // namespace ome
