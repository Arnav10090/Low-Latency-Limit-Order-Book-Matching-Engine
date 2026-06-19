#include "engine_thread.h"

#if defined(__linux__)
#include <pthread.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

using namespace ome;

namespace {

constexpr std::size_t WARMUP_COUNT = 10'000;
constexpr std::size_t BENCH_COUNT = 1'000'000;
constexpr int ENGINE_CORE = 2;
constexpr int PRODUCER_CORE = 3;
constexpr int SEED = 42;
constexpr std::size_t INITIAL_BOOK_SEED_COUNT = 256;
constexpr int CALIBRATION_SAMPLES = 7;
constexpr auto CALIBRATION_WINDOW = std::chrono::milliseconds(250);
constexpr Price MID_PRICE = 100'000;
constexpr int PASSIVE_OFFSET_MIN = 4;
constexpr int PASSIVE_OFFSET_MAX = 24;
constexpr int AGGRESSIVE_OFFSET_MIN = 6;
constexpr int AGGRESSIVE_OFFSET_MAX = 18;

struct ActiveOrder {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};

struct RequestSets {
    std::vector<OrderRequest> warmup;
    std::vector<OrderRequest> measured;
};

struct BenchmarkStats {
    uint64_t mean_ns;
    uint64_t p50_ns;
    uint64_t p95_ns;
    uint64_t p99_ns;
    uint64_t p999_ns;
    uint64_t max_ns;
    uint64_t throughput_ops;
};

struct TickCalibration {
    bool use_tsc;
    double ticks_per_ns;
};

void pauseHint() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

uint64_t readTicksStart() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_lfence();
    return __rdtsc();
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

uint64_t readTicksEnd() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int aux = 0;
    const uint64_t ticks = __rdtscp(&aux);
    _mm_lfence();
    return ticks;
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

TickCalibration calibrateTicks() {
#if defined(__x86_64__) || defined(__i386__)
    std::vector<double> samples;
    samples.reserve(CALIBRATION_SAMPLES);

    for (int sample_index = 0; sample_index < CALIBRATION_SAMPLES; ++sample_index) {
        const auto wall_start = std::chrono::steady_clock::now();
        const uint64_t tick_start = readTicksStart();
        std::this_thread::sleep_for(CALIBRATION_WINDOW);
        const uint64_t tick_end = readTicksEnd();
        const auto wall_end = std::chrono::steady_clock::now();
        const double wall_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count());
        samples.push_back(static_cast<double>(tick_end - tick_start) / wall_ns);
    }

    std::sort(samples.begin(), samples.end());
    return {true, samples[samples.size() / 2]};
#else
    return {false, 1.0};
#endif
}

uint64_t ticksToNanoseconds(uint64_t ticks, const TickCalibration& calibration) {
    if (!calibration.use_tsc) {
        return ticks;
    }
    return static_cast<uint64_t>(static_cast<double>(ticks) / calibration.ticks_per_ns);
}

void setCurrentThreadAffinity(int core_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        throw std::system_error(rc, std::generic_category(), "pthread_setaffinity_np failed for producer thread");
    }
#elif defined(_WIN32)
    const DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL << core_id);
    if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "SetThreadAffinityMask failed for producer thread");
    }
#else
    (void)core_id;
    throw std::runtime_error("Thread affinity is not supported on this platform");
#endif
}

std::string formatNumber(uint64_t value) {
    std::string text = std::to_string(value);
    for (int pos = static_cast<int>(text.size()) - 3; pos > 0; pos -= 3) {
        text.insert(static_cast<std::size_t>(pos), ",");
    }
    return text;
}

uint64_t percentileValue(const std::vector<uint64_t>& sorted, double percentile) {
    if (sorted.empty()) {
        return 0;
    }

    const double raw_index = percentile * static_cast<double>(sorted.size() - 1);
    return sorted[static_cast<std::size_t>(raw_index)];
}

OrderRequest makeNewRequest(RequestId request_id, OrderId id, Side side, Price price, Quantity quantity) {
    return {ActionType::NEW, request_id, id, side, OrderType::LIMIT, price, quantity};
}

Price makePassivePrice(Side side, std::mt19937& rng) {
    std::uniform_int_distribution<int> offset_dist(PASSIVE_OFFSET_MIN, PASSIVE_OFFSET_MAX);
    const Price offset = static_cast<Price>(offset_dist(rng));
    return side == Side::BUY ? MID_PRICE - offset : MID_PRICE + offset;
}

Price makeAggressivePrice(Side side, std::mt19937& rng) {
    std::uniform_int_distribution<int> offset_dist(AGGRESSIVE_OFFSET_MIN, AGGRESSIVE_OFFSET_MAX);
    const Price offset = static_cast<Price>(offset_dist(rng));
    return side == Side::BUY ? MID_PRICE + offset : MID_PRICE - offset;
}

bool crossesBook(Side side, Price price, const std::vector<ActiveOrder>& active_orders) {
    if (side == Side::BUY) {
        Price best_ask = 0;
        bool found_ask = false;
        for (const ActiveOrder& active : active_orders) {
            if (active.side != Side::SELL) {
                continue;
            }
            if (!found_ask || active.price < best_ask) {
                best_ask = active.price;
                found_ask = true;
            }
        }
        return found_ask && price >= best_ask;
    }

    Price best_bid = 0;
    bool found_bid = false;
    for (const ActiveOrder& active : active_orders) {
        if (active.side != Side::BUY) {
            continue;
        }
        if (!found_bid || active.price > best_bid) {
            best_bid = active.price;
            found_bid = true;
        }
    }
    return found_bid && price <= best_bid;
}

void eraseActiveOrder(std::vector<ActiveOrder>& active_orders, std::size_t index) {
    active_orders[index] = active_orders.back();
    active_orders.pop_back();
}

std::size_t findBestOpposingOrder(const std::vector<ActiveOrder>& active_orders, Side incoming_side) {
    const Side resting_side = incoming_side == Side::BUY ? Side::SELL : Side::BUY;
    std::size_t best_index = active_orders.size();

    for (std::size_t index = 0; index < active_orders.size(); ++index) {
        const ActiveOrder& candidate = active_orders[index];
        if (candidate.side != resting_side) {
            continue;
        }

        if (best_index == active_orders.size()) {
            best_index = index;
            continue;
        }

        const ActiveOrder& best = active_orders[best_index];
        const bool is_better_price =
            resting_side == Side::SELL ? candidate.price < best.price : candidate.price > best.price;
        if (is_better_price) {
            best_index = index;
        }
    }

    return best_index;
}

void applySyntheticMatch(std::vector<ActiveOrder>& active_orders,
                         Side incoming_side,
                         Price limit_price,
                         Quantity& remaining_quantity) {
    while (remaining_quantity > 0U) {
        const std::size_t best_index = findBestOpposingOrder(active_orders, incoming_side);
        if (best_index == active_orders.size()) {
            return;
        }

        ActiveOrder& resting = active_orders[best_index];
        const bool crosses =
            incoming_side == Side::BUY ? limit_price >= resting.price : limit_price <= resting.price;
        if (!crosses) {
            return;
        }

        const Quantity matched = (std::min)(remaining_quantity, resting.quantity);
        remaining_quantity -= matched;
        resting.quantity -= matched;
        if (resting.quantity == 0U) {
            eraseActiveOrder(active_orders, best_index);
        }
    }
}

void addOrMatchSyntheticOrder(std::vector<ActiveOrder>& active_orders,
                              OrderId id,
                              Side side,
                              Price price,
                              Quantity quantity) {
    Quantity remaining_quantity = quantity;
    applySyntheticMatch(active_orders, side, price, remaining_quantity);
    if (remaining_quantity > 0U) {
        active_orders.push_back({id, side, price, remaining_quantity});
    }
}

std::size_t selectActiveIndex(const std::vector<ActiveOrder>& active_orders,
                              std::mt19937& rng,
                              OrderId last_request_id) {
    if (active_orders.empty()) {
        return active_orders.size();
    }

    std::uniform_int_distribution<std::size_t> dist(0, active_orders.size() - 1);
    for (std::size_t attempts = 0; attempts < active_orders.size() * 2; ++attempts) {
        const std::size_t index = dist(rng);
        if (active_orders[index].id != last_request_id) {
            return index;
        }
    }

    return active_orders.size();
}

RequestSets generateRequests() {
    RequestSets sets;
    sets.warmup.reserve(WARMUP_COUNT);
    sets.measured.reserve(BENCH_COUNT);

    std::mt19937 rng(SEED);
    std::uniform_real_distribution<double> action_dist(0.0, 1.0);
    std::uniform_real_distribution<double> aggressiveness_dist(0.0, 1.0);
    std::uniform_int_distribution<Quantity> qty_dist(1, 100);

    std::vector<ActiveOrder> active_orders;
    active_orders.reserve(BENCH_COUNT);
    OrderId next_new_id = 1;
    RequestId next_request_id = 1;
    OrderId last_order_id = 0;

    auto append_seed_request = [&](std::vector<OrderRequest>& target, Side side) {
        const Quantity quantity = qty_dist(rng);
        const Price price = makePassivePrice(side, rng);
        const OrderRequest request = makeNewRequest(next_request_id++, next_new_id, side, price, quantity);
        target.push_back(request);
        addOrMatchSyntheticOrder(active_orders, next_new_id, side, price, quantity);
        last_order_id = request.id;
        ++next_new_id;
    };

    auto append_request = [&](std::vector<OrderRequest>& target) {
        while (true) {
            const double action = action_dist(rng);

            if (action < 0.60) {
                const Quantity quantity = qty_dist(rng);
                const bool aggressive = !active_orders.empty() &&
                                        aggressiveness_dist(rng) < 0.45 &&
                                        std::any_of(active_orders.begin(), active_orders.end(), [](const ActiveOrder& order) {
                                            return order.side == Side::SELL;
                                        });
                const Price price = aggressive ? makeAggressivePrice(Side::BUY, rng) : makePassivePrice(Side::BUY, rng);
                const OrderRequest request = makeNewRequest(next_request_id++, next_new_id, Side::BUY, price, quantity);
                target.push_back(request);
                addOrMatchSyntheticOrder(active_orders, next_new_id, Side::BUY, price, quantity);
                last_order_id = request.id;
                ++next_new_id;
                return;
            }

            if (action < 0.80) {
                const Quantity quantity = qty_dist(rng);
                const bool aggressive = !active_orders.empty() &&
                                        aggressiveness_dist(rng) < 0.70 &&
                                        std::any_of(active_orders.begin(), active_orders.end(), [](const ActiveOrder& order) {
                                            return order.side == Side::BUY;
                                        });
                const Price price = aggressive ? makeAggressivePrice(Side::SELL, rng) : makePassivePrice(Side::SELL, rng);
                const OrderRequest request = makeNewRequest(next_request_id++, next_new_id, Side::SELL, price, quantity);
                target.push_back(request);
                addOrMatchSyntheticOrder(active_orders, next_new_id, Side::SELL, price, quantity);
                last_order_id = request.id;
                ++next_new_id;
                return;
            }

            const std::size_t active_index = selectActiveIndex(active_orders, rng, last_order_id);
            if (active_index == active_orders.size()) {
                continue;
            }

            ActiveOrder& active = active_orders[active_index];

            if (action < 0.90) {
                const OrderRequest request{
                    ActionType::CANCEL,
                    next_request_id++,
                    active.id,
                    active.side,
                    OrderType::LIMIT,
                    active.price,
                    0
                };
                target.push_back(request);
                last_order_id = request.id;
                eraseActiveOrder(active_orders, active_index);
                return;
            }

            std::uniform_int_distribution<int> modify_kind_dist(0, 2);
            const int modify_kind = modify_kind_dist(rng);
            Price new_price = active.price;
            Quantity new_quantity = active.quantity;
            const bool opposite_side_exists =
                active.side == Side::BUY
                    ? std::any_of(active_orders.begin(), active_orders.end(), [](const ActiveOrder& order) {
                          return order.side == Side::SELL;
                      })
                    : std::any_of(active_orders.begin(), active_orders.end(), [](const ActiveOrder& order) {
                          return order.side == Side::BUY;
                      });

            if (modify_kind == 0) {
                if (active.quantity > 1U) {
                    std::uniform_int_distribution<Quantity> reduce_dist(1, active.quantity - 1U);
                    new_quantity = reduce_dist(rng);
                }
            } else if (modify_kind == 1) {
                new_quantity = static_cast<Quantity>(active.quantity + qty_dist(rng));
            } else {
                const bool aggressive_reprice = opposite_side_exists && aggressiveness_dist(rng) < 0.65;
                new_price = aggressive_reprice ? makeAggressivePrice(active.side, rng) : makePassivePrice(active.side, rng);
            }

            const OrderRequest request{
                ActionType::MODIFY,
                next_request_id++,
                active.id,
                active.side,
                OrderType::LIMIT,
                new_price,
                new_quantity
            };
            target.push_back(request);
            last_order_id = request.id;

            if (new_quantity == 0U) {
                eraseActiveOrder(active_orders, active_index);
                return;
            }

            if (new_price == active.price && new_quantity < active.quantity) {
                active.quantity = new_quantity;
                return;
            }

            const OrderId order_id = active.id;
            const Side side = active.side;
            eraseActiveOrder(active_orders, active_index);
            addOrMatchSyntheticOrder(active_orders, order_id, side, new_price, new_quantity);
            return;
        }
    };

    for (std::size_t i = 0; i < INITIAL_BOOK_SEED_COUNT && i < WARMUP_COUNT; ++i) {
        append_seed_request(sets.warmup, i % 2 == 0 ? Side::BUY : Side::SELL);
    }

    for (std::size_t i = sets.warmup.size(); i < WARMUP_COUNT; ++i) {
        append_request(sets.warmup);
    }

    for (std::size_t i = 0; i < BENCH_COUNT; ++i) {
        append_request(sets.measured);
    }

    return sets;
}

BenchmarkStats runBenchmark(MatchingEngine& engine,
                            const RequestSets& requests,
                            const TickCalibration& calibration,
                            bool pin_engine) {
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(requests.measured.size());

    engine.start();
    if (pin_engine) {
        engine.setAffinity(ENGINE_CORE);
        setCurrentThreadAffinity(PRODUCER_CORE);
    }

    for (const OrderRequest& request : requests.warmup) {
        while (!engine.submitRequest(request)) {
            pauseHint();
        }
        while (engine.lastProcessedRequestId() != request.request_id) {
            pauseHint();
        }
    }

    const auto wall_start = std::chrono::steady_clock::now();

    for (const OrderRequest& request : requests.measured) {
        const uint64_t start_ticks = readTicksStart();

        while (!engine.submitRequest(request)) {
            pauseHint();
        }

        while (engine.lastProcessedRequestId() != request.request_id) {
            pauseHint();
        }

        const uint64_t end_ticks = readTicksEnd();
        latencies_ns.push_back(ticksToNanoseconds(end_ticks - start_ticks, calibration));
    }

    const auto wall_end = std::chrono::steady_clock::now();
    engine.stop();

    std::sort(latencies_ns.begin(), latencies_ns.end());
    const uint64_t total_ns = std::accumulate(latencies_ns.begin(), latencies_ns.end(), uint64_t(0));
    const double wall_seconds = std::chrono::duration<double>(wall_end - wall_start).count();

    BenchmarkStats stats{};
    stats.mean_ns = total_ns / latencies_ns.size();
    stats.p50_ns = percentileValue(latencies_ns, 0.50);
    stats.p95_ns = percentileValue(latencies_ns, 0.95);
    stats.p99_ns = percentileValue(latencies_ns, 0.99);
    stats.p999_ns = percentileValue(latencies_ns, 0.999);
    stats.max_ns = latencies_ns.back();
    stats.throughput_ops = static_cast<uint64_t>(static_cast<double>(requests.measured.size()) / wall_seconds);
    return stats;
}

} // namespace

int main() {
    const RequestSets requests = generateRequests();
    const TickCalibration calibration = calibrateTicks();

    static MatchingEngine unpinned_engine("AAPL", 10'000, 1'000'000'000, 10'000'000.0);
    static MatchingEngine pinned_engine("AAPL", 10'000, 1'000'000'000, 10'000'000.0);

    const BenchmarkStats unpinned = runBenchmark(unpinned_engine, requests, calibration, false);
    const BenchmarkStats pinned = runBenchmark(pinned_engine, requests, calibration, true);

    std::cout << "=======================================================\n";
    std::cout << "  Dolat Capital - Order Matching Engine Benchmark\n";
    std::cout << "=======================================================\n";
    std::cout << "  Orders        : " << formatNumber(BENCH_COUNT) << "\n";
    std::cout << "  Distribution  : 60% BUY LMT | 20% SELL LMT | 10% CANCEL | 10% MODIFY\n";
    std::cout << "\n";
    std::cout << "  --- Latency (per order, ns) ---\n";
    std::cout << "  Percentile | Unpinned    | Pinned (Core " << ENGINE_CORE << ")\n";
    std::cout << "  -----------------------------------------\n";
    std::cout << "  Mean       | " << std::setw(10) << formatNumber(unpinned.mean_ns)
              << " | " << std::setw(10) << formatNumber(pinned.mean_ns) << "\n";
    std::cout << "  p50        | " << std::setw(10) << formatNumber(unpinned.p50_ns)
              << " | " << std::setw(10) << formatNumber(pinned.p50_ns) << "\n";
    std::cout << "  p95        | " << std::setw(10) << formatNumber(unpinned.p95_ns)
              << " | " << std::setw(10) << formatNumber(pinned.p95_ns) << "\n";
    std::cout << "  p99        | " << std::setw(10) << formatNumber(unpinned.p99_ns)
              << " | " << std::setw(10) << formatNumber(pinned.p99_ns) << "\n";
    std::cout << "  p99.9      | " << std::setw(10) << formatNumber(unpinned.p999_ns)
              << " | " << std::setw(10) << formatNumber(pinned.p999_ns) << "\n";
    std::cout << "  Max        | " << std::setw(10) << formatNumber(unpinned.max_ns)
              << " | " << std::setw(10) << formatNumber(pinned.max_ns) << "\n";
    std::cout << "\n";
    std::cout << "  --- Throughput ---\n";
    std::cout << "  Unpinned Orders/sec : " << formatNumber(unpinned.throughput_ops) << "\n";
    std::cout << "  Pinned Orders/sec   : " << formatNumber(pinned.throughput_ops) << "\n";
    std::cout << "\n";
    std::cout << "  --- Methodology Notes ---\n";
    std::cout << "  Workload      : Seeded resting book with deterministic crossing flow (seed " << SEED << ")\n";
    std::cout << "  Completion    : Per-request polling via lastProcessedRequestId()\n";
    std::cout << "  Timestamps    : LFENCE/RDTSC start | RDTSCP/LFENCE end\n";
    std::cout << "  Calibration   : Median of " << CALIBRATION_SAMPLES
              << " steady_clock windows (" << CALIBRATION_WINDOW.count() << " ms each)\n";
    std::cout << "=======================================================\n";

    return 0;
}
