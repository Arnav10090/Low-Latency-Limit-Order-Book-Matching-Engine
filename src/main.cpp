#include "matching_engine.h"
#include <iostream>
#include <iomanip>

using namespace ome;

int main() {
    std::cout << "=== Dolat Capital — Order Matching Engine Demo ===\n";
    std::cout << "Symbol: AAPL\n\n";

    MatchingEngine engine("AAPL");

    // Step 1: Add SELL LIMIT 10 @ 15050
    std::cout << "[Step 1] Add SELL LIMIT 10 @ 15050 (id=1)\n";
    engine.submit(Side::SELL, OrderType::LIMIT, 15050, 10);
    engine.processAll();

    // Step 2: Add SELL LIMIT 15 @ 15100
    std::cout << "[Step 2] Add SELL LIMIT 15 @ 15100 (id=2)\n";
    engine.submit(Side::SELL, OrderType::LIMIT, 15100, 15);
    engine.processAll();

    // Step 3: Add BUY LIMIT 8 @ 15000
    std::cout << "[Step 3] Add BUY  LIMIT 8  @ 15000 (id=3)\n";
    engine.submit(Side::BUY, OrderType::LIMIT, 15000, 8);
    engine.processAll();
    engine.book().printBook();

    // Step 4: Add BUY LIMIT 12 @ 15050 -- crosses with ask at 15050
    std::cout << "[Step 4] Add BUY LIMIT 12 @ 15050 (id=4)  <-- crosses with ask at 15050\n";
    size_t trades_before = engine.trades().size();
    engine.submit(Side::BUY, OrderType::LIMIT, 15050, 12);
    engine.processAll();
    for (size_t i = trades_before; i < engine.trades().size(); ++i) {
        const auto& t = engine.trades()[i];
        std::cout << ">>> TRADE: buy_id=" << t.buy_order_id
                  << " sell_id=" << t.sell_order_id
                  << " price=" << t.execution_price
                  << " qty=" << t.quantity << "\n";
    }
    engine.book().printBook();

    // Step 5: Add SELL MARKET 5 -- hits best bid at 15050
    std::cout << "[Step 5] Add SELL MARKET 5 (id=5) <-- hits best bid at 15050\n";
    trades_before = engine.trades().size();
    engine.submit(Side::SELL, OrderType::MARKET, 0, 5);
    engine.processAll();
    for (size_t i = trades_before; i < engine.trades().size(); ++i) {
        const auto& t = engine.trades()[i];
        std::cout << ">>> TRADE: buy_id=" << t.buy_order_id
                  << " sell_id=" << t.sell_order_id
                  << " price=" << t.execution_price
                  << " qty=" << t.quantity << "\n";
    }
    engine.book().printBook();

    // Step 6: Cancel order id=3
    std::cout << "[Step 6] Cancel order id=3\n";
    engine.cancel(3);
    engine.processAll();
    std::cout << "Cancelled order 3\n";
    engine.book().printBook();

    // Summary
    std::cout << "=== Summary ===\n";
    std::cout << "Orders processed : " << engine.book().totalOrdersProcessed() << "\n";
    std::cout << "Trades executed  : " << engine.book().totalTradesExecuted() << "\n";

    return 0;
}
