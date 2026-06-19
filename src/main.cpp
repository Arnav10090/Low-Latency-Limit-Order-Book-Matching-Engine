#include "engine_thread.h"

#include <iostream>
#include <string>
#include <thread>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

using namespace ome;

namespace {

RequestId nextRequestId() {
    static RequestId next_request_id = 1;
    return next_request_id++;
}

void pauseHint() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

bool submitAndWait(MatchingEngine& engine, const OrderRequest& request) {
    if (!engine.submitRequest(request)) {
        return false;
    }

    while (engine.lastProcessedRequestId() != request.request_id) {
        pauseHint();
    }

    return true;
}

void printNewTrades(const MatchingEngine& engine, std::size_t trades_before) {
    for (std::size_t i = trades_before; i < engine.trades().size(); ++i) {
        const auto& trade = engine.trades()[i];
        std::cout << ">>> TRADE: buy_id=" << trade.buy_order_id
                  << " sell_id=" << trade.sell_order_id
                  << " price=" << trade.execution_price
                  << " qty=" << trade.quantity << "\n";
    }
}

} // namespace

int main() {
    static MatchingEngine engine("AAPL", 10'000, 100'000, 1'000'000.0);

    std::cout << "=== Dolat Capital - Order Matching Engine Demo ===\n";
    std::cout << "Symbol: AAPL\n\n";

    engine.start();

    const OrderRequest sell_1{ActionType::NEW, nextRequestId(), 1, Side::SELL, OrderType::LIMIT, 15050, 10};
    std::cout << "[Step 1] Add SELL LIMIT 10 @ 15050 (id=1)\n";
    if (!submitAndWait(engine, sell_1)) {
        std::cerr << "Request 1 rejected\n";
        engine.stop();
        return 1;
    }

    const OrderRequest sell_2{ActionType::NEW, nextRequestId(), 2, Side::SELL, OrderType::LIMIT, 15100, 15};
    std::cout << "[Step 2] Add SELL LIMIT 15 @ 15100 (id=2)\n";
    if (!submitAndWait(engine, sell_2)) {
        std::cerr << "Request 2 rejected\n";
        engine.stop();
        return 1;
    }

    const OrderRequest buy_3{ActionType::NEW, nextRequestId(), 3, Side::BUY, OrderType::LIMIT, 15000, 8};
    std::cout << "[Step 3] Add BUY LIMIT 8 @ 15000 (id=3)\n";
    if (!submitAndWait(engine, buy_3)) {
        std::cerr << "Request 3 rejected\n";
        engine.stop();
        return 1;
    }
    engine.book().printBook();

    const OrderRequest modify_reduce_3{ActionType::MODIFY, nextRequestId(), 3, Side::BUY, OrderType::LIMIT, 15000, 5};
    std::cout << "[Step 4] MODIFY order id=3 down to qty=5 at same price (priority preserved)\n";
    if (!submitAndWait(engine, modify_reduce_3)) {
        std::cerr << "Modify 3 reduce rejected\n";
        engine.stop();
        return 1;
    }
    engine.book().printBook();

    const OrderRequest buy_4{ActionType::NEW, nextRequestId(), 4, Side::BUY, OrderType::LIMIT, 15050, 12};
    std::cout << "[Step 5] Add BUY LIMIT 12 @ 15050 (id=4) -- crosses best ask\n";
    std::size_t trades_before = engine.trades().size();
    if (!submitAndWait(engine, buy_4)) {
        std::cerr << "Request 4 rejected\n";
        engine.stop();
        return 1;
    }
    printNewTrades(engine, trades_before);
    engine.book().printBook();

    const OrderRequest modify_increase_3{ActionType::MODIFY, nextRequestId(), 3, Side::BUY, OrderType::LIMIT, 15000, 9};
    std::cout << "[Step 6] MODIFY order id=3 up to qty=9 at same price (priority reset)\n";
    if (!submitAndWait(engine, modify_increase_3)) {
        std::cerr << "Modify 3 increase rejected\n";
        engine.stop();
        return 1;
    }
    engine.book().printBook();

    const OrderRequest modify_price_3{ActionType::MODIFY, nextRequestId(), 3, Side::BUY, OrderType::LIMIT, 15100, 9};
    std::cout << "[Step 7] MODIFY order id=3 to price 15100 (cancel + replace)\n";
    trades_before = engine.trades().size();
    if (!submitAndWait(engine, modify_price_3)) {
        std::cerr << "Modify 3 repricing rejected\n";
        engine.stop();
        return 1;
    }
    printNewTrades(engine, trades_before);
    engine.book().printBook();

    const OrderRequest cancel_4{ActionType::CANCEL, nextRequestId(), 4, Side::BUY, OrderType::LIMIT, 0, 0};
    std::cout << "[Step 8] Cancel order id=4\n";
    if (!submitAndWait(engine, cancel_4)) {
        std::cerr << "Cancel 4 rejected\n";
        engine.stop();
        return 1;
    }
    engine.book().printBook();

    engine.stop();

    std::cout << "=== Summary ===\n";
    std::cout << "Orders processed : " << engine.book().totalOrdersProcessed() << "\n";
    std::cout << "Trades executed  : " << engine.book().totalTradesExecuted() << "\n";

    return 0;
}
