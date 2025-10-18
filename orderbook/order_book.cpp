#include <cstdint> 
#include <vector> 
#include <string> 
#include <unordered_map> 
#include <iostream> 
#include <set> 
using namespace std;

struct TradeOrder {
    uint64_t id;
    bool isBuy;
    double price;
    uint64_t qty;
    uint64_t ts;
};

struct PriceInfo {
    double price;
    uint64_t totalQty;
};

struct BuyCompare {
    bool operator()(const TradeOrder* lhs, const TradeOrder* rhs) const {
        if (lhs->price != rhs->price)
            return lhs->price > rhs->price;  // Higher price first
        return lhs->ts < rhs->ts;           // Earlier timestamp first
    }
};

struct SellCompare {
    bool operator()(const TradeOrder* lhs, const TradeOrder* rhs) const {
        if (lhs->price != rhs->price)
            return lhs->price < rhs->price;  // Lower price first
        return lhs->ts < rhs->ts;
    }
};

class OrderBook {
    unordered_map<uint64_t, TradeOrder> allOrders;
    set<TradeOrder*, BuyCompare> buySet;
    set<TradeOrder*, SellCompare> sellSet;

public:
    // Insert a new order
    void insertOrder(const TradeOrder& order) {
        allOrders[order.id] = order;
        TradeOrder* ptr = &allOrders[order.id];
        if (order.isBuy)
            buySet.insert(ptr);
        else
            sellSet.insert(ptr);
        matchOrders();
    }

    bool removeOrder(uint64_t id) {
        auto it = allOrders.find(id);
        if (it == allOrders.end())
            return false;

        TradeOrder* ord = &it->second;
        if (ord->isBuy) buySet.erase(ord);
        else sellSet.erase(ord);

        allOrders.erase(it);
        return true;
    }

    bool modifyOrder(uint64_t id, double newPrice, uint64_t newQty) {
        auto it = allOrders.find(id);
        if (it == allOrders.end()) return false;

        TradeOrder* ord = &it->second;
        if (ord->isBuy) buySet.erase(ord);
        else sellSet.erase(ord);

        ord->price = newPrice;
        ord->qty = newQty;

        if (ord->isBuy) buySet.insert(ord);
        else sellSet.insert(ord);

        matchOrders();
        return true;
    }

    void getSnapshot(size_t depth, vector<PriceInfo>& bids, vector<PriceInfo>& asks) const {
        bids.clear();
        asks.clear();

        // --- Aggregate buy side ---
        double lastPrice = -1;
        uint64_t accQty = 0;
        size_t added = 0;
        for (auto it = buySet.begin(); it != buySet.end() && added < depth; ++it) {
            const auto* ord = *it;
            if (ord->price != lastPrice) {
                if (lastPrice != -1) {
                    bids.push_back({lastPrice, accQty});
                    if (++added >= depth) break;
                }
                lastPrice = ord->price;
                accQty = ord->qty;
            } else accQty += ord->qty;
        }
        if (lastPrice != -1 && added < depth)
            bids.push_back({lastPrice, accQty});

        // --- Aggregate sell side ---
        lastPrice = -1;
        accQty = 0;
        added = 0;
        for (auto it = sellSet.begin(); it != sellSet.end() && added < depth; ++it) {
            const auto* ord = *it;
            if (ord->price != lastPrice) {
                if (lastPrice != -1) {
                    asks.push_back({lastPrice, accQty});
                    if (++added >= depth) break;
                }
                lastPrice = ord->price;
                accQty = ord->qty;
            } else accQty += ord->qty;
        }
        if (lastPrice != -1 && added < depth)
            asks.push_back({lastPrice, accQty});
    }

    void printBook(size_t depth = 10) const {
        cout << "------ Current Order Book ------" << endl;
        size_t count = 0;
        auto itBuy = buySet.begin();
        auto itSell = sellSet.begin();

        while (count < depth && (itBuy != buySet.end() || itSell != sellSet.end())) {
            if (itBuy != buySet.end()) {
                const auto* b = *itBuy++;
                cout << "BUY  ID: " << b->id << " | P: " << b->price << " | Q: " << b->qty << endl;
                ++count;
            }
            if (count >= depth) break;

            if (itSell != sellSet.end()) {
                const auto* s = *itSell++;
                cout << "SELL ID: " << s->id << " | P: " << s->price << " | Q: " << s->qty << endl;
                ++count;
            }
        }
        cout << "-------------------------------\n";
    }

private:
    void executeTrade(TradeOrder* buy, TradeOrder* sell) {
        cout << "Trade executed: BUY#" << buy->id << " (" << buy->price << ") <-> "
             << "SELL#" << sell->id << " (" << sell->price << ")\n";

        if (buy->qty < sell->qty) {
            sell->qty -= buy->qty;
            buySet.erase(buy);
            allOrders.erase(buy->id);
        } else if (buy->qty > sell->qty) {
            buy->qty -= sell->qty;
            sellSet.erase(sell);
            allOrders.erase(sell->id);
        } else {
            buySet.erase(buy);
            sellSet.erase(sell);
            allOrders.erase(buy->id);
            allOrders.erase(sell->id);
        }
    }

    void matchOrders() {
        while (!buySet.empty() && !sellSet.empty()) {
            TradeOrder* bestBuy = *buySet.begin();
            TradeOrder* bestSell = *sellSet.begin();

            if (bestBuy->price >= bestSell->price)
                executeTrade(bestBuy, bestSell);
            else
                break;
        }
    }
};

int main() {
    OrderBook ob;

    cout << "\n=== ORDER BOOK DEMO ===\n\n";

    // Buy orders
    ob.insertOrder({1001, true, 50.25, 100, 1000000000});
    ob.insertOrder({1011, true, 50.25, 200, 1000000010});
    ob.insertOrder({1002, true, 50.50, 200, 1000000001});
    ob.insertOrder({1003, true, 50.00, 150, 1000000002});

    cout << "After adding buy orders:\n";
    ob.printBook(5);

    // Sell orders
    ob.insertOrder({2001, false, 51.00, 80, 1000000003});
    ob.insertOrder({2002, false, 51.25, 120, 1000000004});
    ob.insertOrder({2003, false, 50.75, 90, 1000000005});
    ob.insertOrder({2004, false, 50.95, 190, 1000000015});

    cout << "After adding sell orders:\n";
    ob.printBook(10);

    // Snapshot
    vector<PriceInfo> bids, asks;
    ob.getSnapshot(4, bids, asks);
    cout << "\nTop 4 Bid Levels:\n";
    for (auto& b : bids)
        cout << "P: " << b.price << " | Qty: " << b.totalQty << endl;

    cout << "\nTop 4 Ask Levels:\n";
    for (auto& a : asks)
        cout << "P: " << a.price << " | Qty: " << a.totalQty << endl;

    // Matching sell
    ob.insertOrder({2005, false, 50.25, 50, 1000000006});
    cout << "\nAfter adding matching sell:\n";
    ob.printBook(10);

    // Cancel orders
    cout << "\nCancelling order 1001...\n";
    cout << (ob.removeOrder(1001) ? "Cancelled successfully" : "Cancel failed") << endl;

    cout << "Cancelling non-existing order 9999...\n";
    cout << (ob.removeOrder(9999) ? "Cancelled successfully" : "Cancel failed") << endl;

    cout << "\nAfter cancellation:\n";
    ob.printBook(10);

    // Amend
    cout << "\nAmending order 1002 (new price: 49.75, qty: 300)\n";
    cout << (ob.modifyOrder(1002, 49.75, 300) ? "Amended successfully" : "Amend failed") << endl;
    ob.printBook(10);

    // Aggressive orders
    ob.insertOrder({3001, true, 52.00, 200, 1000000007});
    ob.insertOrder({3002, false, 49.00, 100, 1000000008});

    cout << "\nFinal order book:\n";
    ob.printBook(10);
    cout << "\n=== END ===\n";
    return 0;
}
