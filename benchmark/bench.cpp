#include "matching_engine.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <chrono>
#include <cstdint>

using namespace ome;

int main() {
    // --- Configuration ---
    constexpr int    WARMUP_ORDERS = 10'000;
    constexpr int    BENCH_ORDERS  = 1'000'000;
    constexpr Price  BASE_PRICE    = 100'000;      // $1000.00
    constexpr Price  MIN_PRICE     = 99'000;
    constexpr Price  MAX_PRICE     = 101'000;
    constexpr int    SEED          = 42;

    // --- RNG setup ---
    std::mt19937 rng(SEED);
    std::normal_distribution<double> price_dist(static_cast<double>(BASE_PRICE), 50.0);
    std::uniform_int_distribution<Quantity> qty_dist(1, 100);
    std::uniform_real_distribution<double> type_dist(0.0, 1.0);

    MatchingEngine engine("AAPL");

    // Track open order IDs for cancel generation
    std::vector<OrderId> open_ids;
    open_ids.reserve(BENCH_ORDERS);

    // --- Helper to generate one order's parameters ---
    auto generate_order = [&](MatchingEngine& eng, std::vector<OrderId>& ids) {
        double r = type_dist(rng);
        Side side;
        OrderType type;
        Price price;
        Quantity qty = qty_dist(rng);

        if (r < 0.60) {
            // 60% BUY LIMIT
            side = Side::BUY;
            type = OrderType::LIMIT;
            price = static_cast<Price>(std::clamp(price_dist(rng), static_cast<double>(MIN_PRICE), static_cast<double>(MAX_PRICE)));
            ids.push_back(eng.nextId());
            eng.submit(side, type, price, qty);
        } else if (r < 0.80) {
            // 20% SELL LIMIT
            side = Side::SELL;
            type = OrderType::LIMIT;
            price = static_cast<Price>(std::clamp(price_dist(rng), static_cast<double>(MIN_PRICE), static_cast<double>(MAX_PRICE)));
            ids.push_back(eng.nextId());
            eng.submit(side, type, price, qty);
        } else if (r < 0.90) {
            // 10% BUY MARKET
            side = Side::BUY;
            type = OrderType::MARKET;
            price = 0;  // market BUY: match at any ask price
            eng.submit(side, type, price, qty);
        } else if (r < 0.95) {
            // 5% SELL MARKET
            side = Side::SELL;
            type = OrderType::MARKET;
            price = INT64_MAX;  // market SELL: match at any bid price
            eng.submit(side, type, price, qty);
        } else {
            // 5% CANCEL
            if (!ids.empty()) {
                std::uniform_int_distribution<size_t> id_dist(0, ids.size() - 1);
                size_t idx = id_dist(rng);
                eng.cancel(ids[idx]);
                // Swap-and-pop for O(1) removal
                ids[idx] = ids.back();
                ids.pop_back();
            } else {
                // No open orders to cancel — submit a BUY LIMIT instead
                side = Side::BUY;
                type = OrderType::LIMIT;
                price = static_cast<Price>(std::clamp(price_dist(rng), static_cast<double>(MIN_PRICE), static_cast<double>(MAX_PRICE)));
                ids.push_back(eng.nextId());
                eng.submit(side, type, price, qty);
            }
        }
    };

    // --- Warm-up phase (discard latencies) ---
    for (int i = 0; i < WARMUP_ORDERS; ++i) {
        generate_order(engine, open_ids);
        engine.processAll();
    }
    engine.clearTrades();

    // --- Benchmark phase ---
    std::vector<uint64_t> latencies;
    latencies.reserve(BENCH_ORDERS);

    auto wall_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < BENCH_ORDERS; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        generate_order(engine, open_ids);
        engine.processAll();  // process just this one order
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
        );
    }

    auto wall_end = std::chrono::high_resolution_clock::now();

    // --- Compute statistics ---
    std::sort(latencies.begin(), latencies.end());

    uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), uint64_t(0));
    uint64_t min_lat  = latencies.front();
    uint64_t max_lat  = latencies.back();
    uint64_t mean_lat = sum / latencies.size();
    uint64_t p50      = latencies[static_cast<size_t>(latencies.size() * 0.50)];
    uint64_t p95      = latencies[static_cast<size_t>(latencies.size() * 0.95)];
    uint64_t p99      = latencies[static_cast<size_t>(latencies.size() * 0.99)];
    uint64_t p99_9    = latencies[static_cast<size_t>(latencies.size() * 0.999)];

    double wall_time_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count()
    );
    double wall_time_ms = wall_time_ns / 1'000'000.0;
    double wall_time_s  = wall_time_ns / 1'000'000'000.0;
    uint64_t throughput = static_cast<uint64_t>(BENCH_ORDERS / wall_time_s);

    // --- Book state ---
    auto bb = engine.book().bestBid();
    auto ba = engine.book().bestAsk();

    // --- Format helper for comma-separated numbers ---
    auto format_num = [](uint64_t n) -> std::string {
        std::string s = std::to_string(n);
        int insert_pos = static_cast<int>(s.length()) - 3;
        while (insert_pos > 0) {
            s.insert(insert_pos, ",");
            insert_pos -= 3;
        }
        return s;
    };

    auto format_price_dollars = [&](Price p) -> std::string {
        std::string dollars = format_num(p / 100);
        std::string cents = std::to_string(p % 100);
        if (cents.length() == 1) cents = "0" + cents;
        return "$" + dollars + "." + cents;
    };

    // --- Print results ---
    std::cout << "=======================================================\n";
    std::cout << "  Dolat Capital — Order Matching Engine Benchmark\n";
    std::cout << "=======================================================\n";
    std::cout << "  Symbol        : AAPL\n";
    std::cout << "  Orders        : " << format_num(BENCH_ORDERS) << "\n";
    std::cout << "  Distribution  : 60% BUY LMT | 20% SELL LMT | 10% BUY MKT | 5% SELL MKT | 5% CANCEL\n";
    std::cout << "\n";
    std::cout << "  --- Latency (per order) ---\n";
    std::cout << "  Min      : " << std::setw(10) << format_num(min_lat) << " ns\n";
    std::cout << "  Mean     : " << std::setw(10) << format_num(mean_lat) << " ns\n";
    std::cout << "  p50      : " << std::setw(10) << format_num(p50) << " ns\n";
    std::cout << "  p95      : " << std::setw(10) << format_num(p95) << " ns\n";
    std::cout << "  p99      : " << std::setw(10) << format_num(p99) << " ns\n";
    std::cout << "  p99.9    : " << std::setw(10) << format_num(p99_9) << " ns\n";
    std::cout << "  Max      : " << std::setw(10) << format_num(max_lat) << " ns\n";
    std::cout << "\n";
    std::cout << "  --- Throughput ---\n";
    std::cout << "  Total time   : " << static_cast<int>(wall_time_ms) << " ms\n";
    std::cout << "  Orders/sec   : " << format_num(throughput) << "\n";
    std::cout << "\n";
    std::cout << "  --- Book State (end of run) ---\n";
    if (bb) {
        std::cout << "  Best Bid     : " << format_num(*bb) << " (" << format_price_dollars(*bb) << ")\n";
    } else {
        std::cout << "  Best Bid     : (empty)\n";
    }
    if (ba) {
        std::cout << "  Best Ask     : " << format_num(*ba) << " (" << format_price_dollars(*ba) << ")\n";
    } else {
        std::cout << "  Best Ask     : (empty)\n";
    }
    std::cout << "  Bid levels   : " << engine.book().bidDepth() << "\n";
    std::cout << "  Ask levels   : " << engine.book().askDepth() << "\n";
    std::cout << "\n";
    std::cout << "  --- Matching Stats ---\n";
    std::cout << "  Trades executed : " << format_num(engine.book().totalTradesExecuted()) << "\n";

    std::cout << "  Orders in book  : " << format_num(engine.book().ordersInBook()) << "\n";
    std::cout << "=======================================================\n";

    return 0;
}
