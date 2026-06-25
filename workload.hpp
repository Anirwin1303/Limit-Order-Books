#pragma once
//
// Deterministic replay workload.
//
// Same seed -> same Op stream -> same engine behavior -> same trades, every
// run and across both engines. That is the reproducibility the brief requires,
// and in Stage 1 it doubles as the correctness oracle: the flat engine must
// produce a trade stream whose checksum equals the naive engine's.
//
// The stream exercises all three hot paths in a realistic mix:
//   - add_passive    : a limit that rests away from the touch (no trade)
//   - add_marketable : a limit that crosses and sweeps one or more levels
//   - cancel         : remove a random live resting order (the scan-to-cancel path)
//
#include "types.hpp"

#include <vector>
#include <random>
#include <cstdint>
#include <string>
#include <cstdio>

namespace bench {
using namespace ob;

enum Cat : std::uint8_t { ADD_PASSIVE = 0, ADD_MARKETABLE = 1, CANCEL = 2, NCAT = 3 };

inline const char* cat_name(std::uint8_t c) {
    switch (c) {
        case ADD_PASSIVE:    return "add_passive";
        case ADD_MARKETABLE: return "add_marketable";
        case CANCEL:         return "cancel";
        default:             return "?";
    }
}

struct WorkloadParams {
    std::uint64_t seed         = 0x9E3779B97F4A7C15ull; // golden ratio; fixed
    Price         mid          = 100000;                // ticks
    Price         band         = 1000;                  // resting levels each side
    Quantity      depth_qty    = 10;                    // qty per populate level
    std::size_t   n_stream     = 1100000;               // total measured ops (warmup included)
    Quantity      passive_qty  = 5;
    Price         cross_reach  = 8;                     // ticks a marketable order reaches past mid
    double        p_passive    = 0.45;
    double        p_marketable = 0.25;
    // remainder (0.30) -> cancel
};

struct Workload {
    std::vector<Op>           populate;  // applied untimed to build book depth
    std::vector<Op>           stream;    // measured (after a warmup prefix)
    std::vector<std::uint8_t> cat;       // category per stream op
    std::string               description;
    WorkloadParams            params;
};

inline Workload make_workload(const WorkloadParams& p = {}) {
    Workload w;
    w.params = p;
    std::mt19937_64 rng(p.seed);
    OrderId next = 1;

    std::vector<OrderId> live;   // resting passive ids eligible for cancel
    live.reserve(p.n_stream + 2 * p.band);

    // Populate: one resting order per level on each side, building a deep book
    // around `mid` so marketable orders have something to sweep.
    for (Price d = 1; d <= p.band; ++d) {
        Op b = Op::add(next++, Side::Buy,  p.mid - d, p.depth_qty);
        Op a = Op::add(next++, Side::Sell, p.mid + d, p.depth_qty);
        w.populate.push_back(b);
        w.populate.push_back(a);
        live.push_back(b.id);
        live.push_back(a.id);
    }

    std::uniform_real_distribution<double> coin(0.0, 1.0);
    std::uniform_int_distribution<Price>   passive_off(1, p.band);
    std::uniform_int_distribution<Price>   cross_off(0, (int)p.cross_reach);
    std::uniform_int_distribution<Quantity> mkt_qty(1, 25);
    std::bernoulli_distribution            buy_coin(0.5);

    w.stream.reserve(p.n_stream);
    w.cat.reserve(p.n_stream);

    auto push_passive = [&](void) {
        bool buy   = buy_coin(rng);
        Price price = buy ? (p.mid - passive_off(rng)) : (p.mid + passive_off(rng));
        Op o = Op::add(next++, buy ? Side::Buy : Side::Sell, price, p.passive_qty);
        w.stream.push_back(o);
        w.cat.push_back(ADD_PASSIVE);
        live.push_back(o.id);
    };

    for (std::size_t i = 0; i < p.n_stream; ++i) {
        double u = coin(rng);
        if (u < p.p_passive) {
            push_passive();
        } else if (u < p.p_passive + p.p_marketable) {
            bool buy    = buy_coin(rng);
            Price price = buy ? (p.mid + cross_off(rng)) : (p.mid - cross_off(rng));
            Op o = Op::add(next++, buy ? Side::Buy : Side::Sell, price, mkt_qty(rng));
            w.stream.push_back(o);
            w.cat.push_back(ADD_MARKETABLE);
            // a remainder may rest; we deliberately don't track it for cancel
        } else {
            if (live.empty()) {
                push_passive();   // nothing to cancel yet
            } else {
                std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
                std::size_t k = pick(rng);
                OrderId id = live[k];
                live[k] = live.back();
                live.pop_back();
                w.stream.push_back(Op::cancel(id));
                w.cat.push_back(CANCEL);
                // may already be filled -> engine no-ops, which is realistic
            }
        }
    }

    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "seed=0x%llx mid=%lld band=%lld(levels/side) depth=%llu/level "
        "stream=%zu mix[passive=%.2f marketable=%.2f cancel=%.2f] "
        "passive_qty=%llu marketable_qty=U[1,25] cross_reach=%lld",
        (unsigned long long)p.seed, (long long)p.mid, (long long)p.band,
        (unsigned long long)p.depth_qty, p.n_stream, p.p_passive, p.p_marketable,
        1.0 - p.p_passive - p.p_marketable, (unsigned long long)p.passive_qty,
        (long long)p.cross_reach);
    w.description = buf;
    return w;
}

} // namespace bench
