// main.cpp
#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include <set>
#include <list>
#include <algorithm>
using namespace std;

struct Order {
    uint64_t order_id;     // Unique order identifier
    bool is_buy;           // true = buy, false = sell
    double price;          // Limit price
    uint64_t quantity;     // Remaining quantity
    uint64_t timestamp_ns; // Order entry timestamp in nanoseconds
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
};

// Comparators for ordering pointers to Orders.
// Must impose a strict-weak ordering and break ties with order_id to allow duplicate timestamps.
struct BuyCompare {
    bool operator()(const Order* lhs, const Order* rhs) const {
        if (lhs->price != rhs->price) return lhs->price > rhs->price;       // higher price first
        if (lhs->timestamp_ns != rhs->timestamp_ns) return lhs->timestamp_ns < rhs->timestamp_ns; // FIFO
        return lhs->order_id < rhs->order_id;                               // unique tie-break
    }
};
struct SellCompare {
    bool operator()(const Order* lhs, const Order* rhs) const {
        if (lhs->price != rhs->price) return lhs->price < rhs->price;       // lower price first
        if (lhs->timestamp_ns != rhs->timestamp_ns) return lhs->timestamp_ns < rhs->timestamp_ns; // FIFO
        return lhs->order_id < rhs->order_id;                               // unique tie-break
    }
};

class OrderBook {
public:
    // Insert a new order into the book
    void add_order(const Order& order);

    // Cancel an existing order by its ID
    bool cancel_order(uint64_t order_id);

    // Amend an existing order's price or quantity
    bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity);

    // Get a snapshot of top N bid and ask levels (aggregated quantities)
    void get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const;

    // Print current state of the order book
    void print_book(size_t depth = 10) const;

    OrderBook() = default;
    ~OrderBook() = default;

private:
    // Stable storage of orders. list nodes have stable addresses.
    std::list<Order> orders;

    // Map order_id -> iterator in orders list for O(1) lookup
    std::unordered_map<uint64_t, std::list<Order>::iterator> order_lookup;

    // Ordered sets of pointers to orders for fast best bid/ask retrieval
    std::set<Order*, BuyCompare> buy_book;
    std::set<Order*, SellCompare> sell_book;

    // Matching engine: simple matching while best bid >= best ask
    void match();

    // Helpers to safely remove an order from book (set + list + map)
    void remove_order_by_iterator(const std::list<Order>::iterator& it);
};

void OrderBook::add_order(const Order& order) {
    // Insert into list (stable storage)
    orders.emplace_back(order);
    auto it = std::prev(orders.end());

    // Register in lookup
    order_lookup[it->order_id] = it;

    // Insert pointer into appropriate ordered set
    Order* ptr = &(*it);
    if (ptr->is_buy) buy_book.insert(ptr);
    else sell_book.insert(ptr);

    // Optional: try matching
    match();
}

bool OrderBook::cancel_order(uint64_t order_id) {
    auto it_lookup = order_lookup.find(order_id);
    if (it_lookup == order_lookup.end()) return false;

    auto it = it_lookup->second;
    Order* ptr = &(*it);

    // Erase from ordered set first
    if (ptr->is_buy) buy_book.erase(ptr);
    else sell_book.erase(ptr);

    // Erase from list and lookup
    orders.erase(it);
    order_lookup.erase(it_lookup);
    return true;
}

bool OrderBook::amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) {
    auto it_lookup = order_lookup.find(order_id);
    if (it_lookup == order_lookup.end()) return false;

    auto it = it_lookup->second;
    Order* ord = &(*it);

    // If price changed, must remove and re-insert in the ordered set to maintain correct ordering.
    if (ord->price != new_price) {
        if (ord->is_buy) buy_book.erase(ord);
        else sell_book.erase(ord);

        ord->price = new_price;
        ord->quantity = new_quantity;

        if (ord->is_buy) buy_book.insert(ord);
        else sell_book.insert(ord);

        // Price changed — matching might now be possible
        match();
        return true;
    }

    // If only quantity changed, update in place
    ord->quantity = new_quantity;
    if (ord->quantity == 0) {
        // If quantity becomes 0, remove the order completely
        if (ord->is_buy) buy_book.erase(ord);
        else sell_book.erase(ord);

        orders.erase(it);
        order_lookup.erase(it_lookup);
    }
    return true;
}

void OrderBook::get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const {
    bids.clear();
    asks.clear();

    // Aggregate bids (buy_book is ordered highest price first)
    double current_price = numeric_limits<double>::quiet_NaN();
    uint64_t total_qty = 0;
    size_t added_levels = 0;

    for (auto it = buy_book.begin(); it != buy_book.end() && added_levels < depth; ++it) {
        const Order* o = *it;
        if (!(o->price == current_price)) { // handles NaN initial
            if (!std::isnan(current_price)) {
                bids.push_back({current_price, total_qty});
                ++added_levels;
                if (added_levels >= depth) break;
            }
            current_price = o->price;
            total_qty = o->quantity;
        } else {
            total_qty += o->quantity;
        }
    }
    if (!std::isnan(current_price) && added_levels < depth) {
        bids.push_back({current_price, total_qty});
    }

    // Aggregate asks (sell_book is ordered lowest price first)
    current_price = numeric_limits<double>::quiet_NaN();
    total_qty = 0;
    added_levels = 0;
    for (auto it = sell_book.begin(); it != sell_book.end() && added_levels < depth; ++it) {
        const Order* o = *it;
        if (!(o->price == current_price)) {
            if (!std::isnan(current_price)) {
                asks.push_back({current_price, total_qty});
                ++added_levels;
                if (added_levels >= depth) break;
            }
            current_price = o->price;
            total_qty = o->quantity;
        } else {
            total_qty += o->quantity;
        }
    }
    if (!std::isnan(current_price) && added_levels < depth) {
        asks.push_back({current_price, total_qty});
    }
}

void OrderBook::print_book(size_t depth) const {
    cout << "======================================" << endl;
    cout << "Order Book (interleaved up to " << depth << " entries):" << endl;

    auto it_buy = buy_book.begin();
    auto it_sell = sell_book.begin();
    size_t count = 0;

    while (count < depth && (it_buy != buy_book.end() || it_sell != sell_book.end())) {
        if (it_buy != buy_book.end()) {
            const Order* o = *it_buy;
            cout << "[BID]  ID: " << o->order_id << " | P: " << o->price << " | Q: " << o->quantity << " | ts: " << o->timestamp_ns << "\n";
            ++it_buy;
            ++count;
            if (count >= depth) break;
        }
        if (it_sell != sell_book.end()) {
            const Order* o = *it_sell;
            cout << "[ASK]  ID: " << o->order_id << " | P: " << o->price << " | Q: " << o->quantity << " | ts: " << o->timestamp_ns << "\n";
            ++it_sell;
            ++count;
        }
    }
    cout << "======================================" << endl;
}

void OrderBook::remove_order_by_iterator(const std::list<Order>::iterator& it) {
    Order* ptr = &(*it);
    if (ptr->is_buy) buy_book.erase(ptr);
    else sell_book.erase(ptr);
    order_lookup.erase(ptr->order_id);
    orders.erase(it);
}

void OrderBook::match() {
    // Simple continuous matching while best bid >= best ask
    while (!buy_book.empty() && !sell_book.empty()) {
        Order* best_buy = *buy_book.begin();
        Order* best_sell = *sell_book.begin();

        if (best_buy->price < best_sell->price) break; // no match possible

        // execution qty is min of both
        uint64_t trade_qty = std::min(best_buy->quantity, best_sell->quantity);
        double trade_price = best_sell->timestamp_ns <= best_buy->timestamp_ns ? best_sell->price : best_buy->price;
        // (we can choose trade price policy; here we show both sides' prices for clarity)
        cout << "TRADE: BuyID=" << best_buy->order_id << " SellID=" << best_sell->order_id
             << " Qty=" << trade_qty << " BidP=" << best_buy->price << " AskP=" << best_sell->price << endl;

        // Deduct quantities
        best_buy->quantity -= trade_qty;
        best_sell->quantity -= trade_qty;

        // Remove any orders with quantity == 0
        if (best_buy->quantity == 0) {
            // find iterator in map to erase list node safely
            auto itb = order_lookup.find(best_buy->order_id);
            if (itb != order_lookup.end()) {
                auto list_it = itb->second;
                // Erase from sets & list & map
                buy_book.erase(best_buy);            // erase pointer from set
                order_lookup.erase(itb);             // remove lookup
                orders.erase(list_it);               // remove list node
            } else {
                // Shouldn't happen; safety guard
                buy_book.erase(best_buy);
            }
        }

        if (best_sell->quantity == 0) {
            auto its = order_lookup.find(best_sell->order_id);
            if (its != order_lookup.end()) {
                auto list_it = its->second;
                sell_book.erase(best_sell);
                order_lookup.erase(its);
                orders.erase(list_it);
            } else {
                sell_book.erase(best_sell);
            }
        }

        // If one side partially filled, its pointer remains valid and still in the set.
        // continue matching while condition holds
    }
}

// ---------------------------
// Demo / Tests (main)
// ---------------------------
int main() {
    OrderBook ob;

    cout << "=== Testing Order Book Implementation ===" << endl << endl;

    // Test 1: Add some buy orders
    cout << "1. Adding buy orders:" << endl;
    Order buy1 = {1001, true, 50.25, 100, 1000000000};
    Order buy4 = {1011, true, 50.25, 200, 1000000010};
    Order buy2 = {1002, true, 50.50, 200, 1000000001};
    Order buy3 = {1003, true, 50.00, 150, 1000000002};

    ob.add_order(buy1);
    ob.add_order(buy2);
    ob.add_order(buy3);
    ob.add_order(buy4);

    cout << "==========Book state after adding buy orders:==========" << endl;
    ob.print_book(5);
    cout << endl;

    // Test 2: Add some sell orders (no matching yet)
    cout << "2. Adding sell orders (higher prices, no matches):" << endl;
    Order sell1 = {2001, false, 51.00, 80, 1000000003};
    Order sell2 = {2002, false, 51.25, 120, 1000000004};
    Order sell3 = {2003, false, 50.75, 90, 1000000005};
    Order sell4 = {2004, false, 50.95, 190, 1000000015};

    ob.add_order(sell1);
    ob.add_order(sell2);
    ob.add_order(sell3);
    ob.add_order(sell4);

    cout << "==========Book state after adding sell orders:==========" << endl;
    ob.print_book(10);
    cout << endl;

    // Test 3: Snapshot
    cout << "3. Testing snapshot functionality:" << endl;
    vector<PriceLevel> bids, asks;
    ob.get_snapshot(4, bids, asks);

    cout << "==========Top 4 Aggregated Bids:==========" << endl;
    for (const auto& b : bids) {
        cout << "  Price: " << b.price << ", Quantity: " << b.total_quantity << endl;
    }
    cout << "==========Top 4 Aggregated Asks:==========" << endl;
    for (const auto& a : asks) {
        cout << "  Price: " << a.price << ", Quantity: " << a.total_quantity << endl;
    }
    cout << endl;

    // Test 4: Add a sell order that will match
    cout << "4. Adding a sell order (id: 2005, qty: 50, price: 50.25) that should match:" << endl;
    Order sell_match = {2005, false, 50.25, 50, 1000000006};
    ob.add_order(sell_match);

    cout << "==========Book state after matching:==========" << endl;
    ob.print_book(10);
    cout << endl;

    // Test 5: Cancel
    cout << "5. Testing order cancellation:" << endl;
    cout << "Canceling order ID 1001..." << endl;
    bool cancelled = ob.cancel_order(1001);
    cout << "Cancellation " << (cancelled ? "successful" : "failed") << endl;

    cout << "Trying to cancel non-existent order 9999..." << endl;
    cancelled = ob.cancel_order(9999);
    cout << "Cancellation " << (cancelled ? "successful" : "failed") << endl;

    cout << "==========Book state after cancellation:==========" << endl;
    ob.print_book(10);
    cout << endl;

    // Test 6: Amend
    cout << "6. Testing order amendment:" << endl;
    cout << "Amending order (for ID 1002) - changing price to 49.75 and quantity to 300..." << endl;
    bool amended = ob.amend_order(1002, 49.75, 300);
    cout << "Amendment " << (amended ? "successful" : "failed") << endl;

    cout << "==========Book state after amendment:==========" << endl;
    ob.print_book(10);
    cout << endl;

    // Test 7: Aggressive orders to trigger matches
    cout << "7. Adding aggressive orders to trigger matches:" << endl;
    Order aggressive_buy = {3001, true, 52.00, 200, 1000000007};
    Order aggressive_sell = {3002, false, 49.00, 100, 1000000008};

    cout << "Adding aggressive buy order (price: 52.00)..." << endl;
    ob.add_order(aggressive_buy);

    cout << "Adding aggressive sell order (price: 49.00)..." << endl;
    ob.add_order(aggressive_sell);

    cout << "==========Final book state:==========" << endl;
    ob.print_book(10);

    cout << endl << "=== Order Book Testing Complete ===" << endl;
    return 0;
}
