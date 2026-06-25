#pragma once
//
// Shared domain model for the limit order book project.
//
// Everything here is shared between Stage 0 (naive) and Stage 1 (flat) so that
// the correctness tests and the latency harness are written ONCE and run
// against either engine. The `Engine` concept is the "shared interface" from
// the brief: a compile-time contract, so swapping engines costs no virtual
// dispatch on the hot path.
//
#include <cstdint>
#include <optional>
#include <vector>
#include <concepts>

namespace ob {

// Integer tick prices. Never float: floats cannot represent most decimal
// prices exactly, so ordering and equality quietly break. A "price" here is a
// count of ticks from some reference; the engine never needs to know the tick
// size.
using Price    = std::int64_t;
using Quantity = std::uint64_t;
using OrderId  = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

// A resting or incoming order. `qty` is the *remaining* quantity.
struct Order {
    OrderId  id;
    Side     side;
    Price    price;
    Quantity qty;
};

// A trade (fill). By exchange convention the execution price is the RESTING
// (maker) order's price, so the aggressor receives any price improvement.
struct Trade {
    OrderId  maker_id;  // the resting order that was hit
    OrderId  taker_id;  // the incoming aggressor
    Price    price;     // maker's resting price
    Quantity qty;
};

enum class OpType : std::uint8_t { Add = 0, Cancel = 1 };

// One input message in a deterministic replay stream.
// For Cancel, only `id` is meaningful.
struct Op {
    OpType   type;
    OrderId  id;
    Side     side;
    Price    price;
    Quantity qty;

    static Op add(OrderId id, Side side, Price price, Quantity qty) {
        return Op{OpType::Add, id, side, price, qty};
    }
    static Op cancel(OrderId id) {
        return Op{OpType::Cancel, id, Side::Buy, 0, 0};
    }
};

// The shared interface. Any engine usable by the tests and the harness must:
//   - process one Op, appending any resulting trades to `out`
//   - expose enough state to assert correctness (top of book, depth, count)
//
// `submit` takes the trade sink by reference so the caller owns the buffer and
// can reuse its capacity across calls. That keeps harness plumbing out of the
// measured number while still letting the ENGINE'S OWN allocations (map nodes,
// deque growth) show up honestly in the baseline.
template <class E>
concept Engine = requires(E e, const E ce, const Op& op,
                          std::vector<Trade>& out, Side side, Price price) {
    { e.submit(op, out) }        -> std::same_as<void>;
    { ce.best_bid() }            -> std::same_as<std::optional<Price>>;
    { ce.best_ask() }            -> std::same_as<std::optional<Price>>;
    { ce.qty_at(side, price) }   -> std::same_as<Quantity>;
    { ce.resting_count() }       -> std::same_as<std::size_t>;
    { e.clear() }                -> std::same_as<void>;
};

} // namespace ob
