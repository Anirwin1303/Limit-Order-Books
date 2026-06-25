#pragma once
//
// Stage 0 — the deliberately naive baseline.
//
// Data structures, exactly as specified in the brief and chosen to be the
// "left panel" of the memory-hierarchy picture (scattered tree nodes,
// pointer-chasing, per-order allocation):
//
//   - Each side is a std::map<Price, std::deque<Order>>.
//       * the map keeps price levels sorted, so the best level is begin()
//       * the deque keeps strict FIFO time priority within a level
//   - A std::unordered_map<OrderId, Loc> indexes every resting order so cancel
//     finds the LEVEL in O(1) instead of scanning all price levels.
//
// The intentional weaknesses we are NOT fixing here (Stage 1's job):
//   1. Tree of price levels  -> pointer-chasing, cache-hostile traversal.
//   2. Per-order allocation   -> map nodes + deque growth allocate on the hot
//                                path; tail latency blows up under load.
//   3. Scan-to-cancel         -> even with the O(1) level lookup, removing the
//                                order from the middle of the deque is a linear
//                                scan + shift. This is the O(n)-in-level cost
//                                the intrusive list kills in Stage 1.
//
#include "types.hpp"

#include <map>
#include <deque>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <optional>

namespace ob {

class NaiveOrderBook {
public:
    void submit(const Op& op, std::vector<Trade>& out) {
        switch (op.type) {
            case OpType::Add:    add(op, out);     break;
            case OpType::Cancel: cancel(op.id);    break;
        }
    }

    std::optional<Price> best_bid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;          // greater<> => highest price first
    }
    std::optional<Price> best_ask() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;          // less<>    => lowest price first
    }

    Quantity qty_at(Side side, Price price) const {
        Quantity total = 0;
        if (side == Side::Buy) {
            auto it = bids_.find(price);
            if (it != bids_.end()) for (const auto& o : it->second) total += o.qty;
        } else {
            auto it = asks_.find(price);
            if (it != asks_.end()) for (const auto& o : it->second) total += o.qty;
        }
        return total;
    }

    std::size_t resting_count() const { return index_.size(); }

    void clear() {
        bids_.clear();
        asks_.clear();
        index_.clear();
    }

private:
    // bids: highest price is the best  -> sort descending
    using BidBook = std::map<Price, std::deque<Order>, std::greater<Price>>;
    // asks: lowest price is the best   -> sort ascending
    using AskBook = std::map<Price, std::deque<Order>, std::less<Price>>;

    struct Loc { Side side; Price price; };

    BidBook bids_;
    AskBook asks_;
    std::unordered_map<OrderId, Loc> index_;

    void add(const Op& op, std::vector<Trade>& out) {
        Order incoming{op.id, op.side, op.price, op.qty};

        // Try to cross against the opposite side first.
        if (incoming.side == Side::Buy) {
            // a buy crosses an ask level whose price is <= the buy's limit
            match(asks_, incoming, out,
                  [](Price level, Price limit) { return level <= limit; });
        } else {
            // a sell crosses a bid level whose price is >= the sell's limit
            match(bids_, incoming, out,
                  [](Price level, Price limit) { return level >= limit; });
        }

        // Whatever did not fill rests on the book.
        if (incoming.qty > 0) {
            if (incoming.side == Side::Buy) bids_[incoming.price].push_back(incoming);
            else                            asks_[incoming.price].push_back(incoming);
            index_[incoming.id] = Loc{incoming.side, incoming.price};
        }
    }

    // Generic matcher: `crosses(level_price, incoming_limit)` decides whether the
    // best level of `opp` is marketable against the incoming order. Works for
    // both sides because each book's begin() is already its best level.
    template <class MapT, class Crosses>
    void match(MapT& opp, Order& incoming, std::vector<Trade>& out, Crosses crosses) {
        while (incoming.qty > 0 && !opp.empty()) {
            auto level_it = opp.begin();                 // best price level
            if (!crosses(level_it->first, incoming.price)) break;

            auto& queue = level_it->second;              // FIFO at this level
            while (incoming.qty > 0 && !queue.empty()) {
                Order& resting = queue.front();          // oldest order first
                const Quantity exec = std::min(incoming.qty, resting.qty);

                // Execute at the RESTING order's price (price improvement to aggressor).
                out.push_back(Trade{resting.id, incoming.id, resting.price, exec});

                incoming.qty -= exec;
                resting.qty  -= exec;

                if (resting.qty == 0) {
                    index_.erase(resting.id);
                    queue.pop_front();                   // O(1) at the front
                }
                // else: incoming is exhausted; loop condition ends it.
            }

            if (queue.empty()) opp.erase(level_it);      // drop empty price level
        }
    }

    void cancel(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) return;                  // unknown / already filled: no-op
        const Loc loc = it->second;
        if (loc.side == Side::Buy) erase_from(bids_, loc.price, id);
        else                       erase_from(asks_, loc.price, id);
        index_.erase(it);
    }

    // The deliberately naive removal: O(1) to find the level via the price, then
    // a LINEAR scan of the deque to find the id, then an erase that shifts the
    // tail. This is the "scan-to-cancel" weakness.
    template <class MapT>
    void erase_from(MapT& book, Price price, OrderId id) {
        auto level_it = book.find(price);
        if (level_it == book.end()) return;
        auto& queue = level_it->second;
        for (auto i = queue.begin(); i != queue.end(); ++i) {
            if (i->id == id) { queue.erase(i); break; }
        }
        if (queue.empty()) book.erase(level_it);
    }
};

static_assert(Engine<NaiveOrderBook>, "NaiveOrderBook must satisfy the Engine concept");

} // namespace ob
