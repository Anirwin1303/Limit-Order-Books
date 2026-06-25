//
// Stage 0 latency + throughput harness.
//
// Templated on the Engine concept so Stage 1 reuses every line. Run structure
// per engine, both starting from an identical freshly-populated book:
//   1. LATENCY pass   : bracket each op with two clock reads, subtract the
//                       measured clock overhead, bucket by category, summarize
//                       p50/p90/p99/p99.9. A warmup prefix is processed but not
//                       sampled.
//   2. THROUGHPUT pass : no per-op timestamps (so sampling cost doesn't deflate
//                       the rate); bracket the whole stream once.
// Both passes checksum the trade stream; the two must agree, and in Stage 1 the
// flat engine's checksum must equal the naive engine's.
//
#include "types.hpp"
#include "naive_book.hpp"
#include "timing.hpp"
#include "workload.hpp"

#include <cstdio>
#include <cstdint>
#include <vector>

#ifdef __linux__
#include <sched.h>
#endif

#ifndef BUILD_FLAGS
#define BUILD_FLAGS "(unset)"
#endif

using namespace ob;
using namespace bench;

// FNV-1a over the trade stream: cheap, order-sensitive integrity fingerprint.
struct TradeHash {
    std::uint64_t h = 1469598103934665603ull;
    void feed(std::uint64_t x) {
        for (int i = 0; i < 8; ++i) { h ^= (x & 0xff); h *= 1099511628211ull; x >>= 8; }
    }
    void feed_trade(const Trade& t) {
        feed(t.maker_id); feed(t.taker_id);
        feed(static_cast<std::uint64_t>(t.price)); feed(t.qty);
    }
};

static void pin_cpu0() {
#ifdef __linux__
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
    int rc = sched_setaffinity(0, sizeof(set), &set);
    std::printf("cpu pinning      : %s\n", rc == 0 ? "pinned to cpu0" : "FAILED (continuing unpinned)");
#else
    std::printf("cpu pinning      : unsupported on this platform\n");
#endif
}

template <Engine E>
static void populate(E& e, const Workload& w) {
    std::vector<Trade> tr; tr.reserve(64);
    for (const auto& op : w.populate) { tr.clear(); e.submit(op, tr); }
}

template <Engine E>
static std::uint64_t latency_pass(E& e, const Workload& w, std::size_t warmup,
                                  std::uint64_t clock_med,
                                  std::vector<std::uint64_t> (&buckets)[NCAT],
                                  std::size_t& trades_out) {
    std::vector<Trade> tr; tr.reserve(256);
    TradeHash hash;
    std::size_t trades = 0;

    for (auto& b : buckets) { b.clear(); b.reserve(w.stream.size()); }

    for (std::size_t i = 0; i < w.stream.size(); ++i) {
        tr.clear();
        const std::uint64_t t0 = now_ns();
        e.submit(w.stream[i], tr);
        const std::uint64_t t1 = now_ns();
        do_not_optimize(tr);

        // Checksum is computed OUTSIDE the timed bracket.
        for (const auto& t : tr) hash.feed_trade(t);
        trades += tr.size();

        if (i >= warmup) {
            std::uint64_t d = t1 - t0;
            d = (d > clock_med) ? (d - clock_med) : 0;   // subtract clock overhead, floor 0
            buckets[w.cat[i]].push_back(d);
        }
    }
    trades_out = trades;
    return hash.h;
}

template <Engine E>
static std::uint64_t throughput_pass(E& e, const Workload& w,
                                     double& msgs_per_sec, std::size_t& trades_out) {
    std::vector<Trade> tr; tr.reserve(256);
    TradeHash hash;
    std::size_t trades = 0;

    const std::uint64_t t0 = now_ns();
    for (std::size_t i = 0; i < w.stream.size(); ++i) {
        tr.clear();
        e.submit(w.stream[i], tr);
        do_not_optimize(tr);
        for (const auto& t : tr) hash.feed_trade(t);   // negligible vs map ops; identical across stages
        trades += tr.size();
    }
    const std::uint64_t t1 = now_ns();

    const double secs = static_cast<double>(t1 - t0) / 1e9;
    msgs_per_sec = static_cast<double>(w.stream.size()) / secs;
    trades_out = trades;
    return hash.h;
}

template <Engine E>
static void run_engine(const char* name, const Workload& w, std::size_t warmup,
                       std::uint64_t clock_med) {
    std::printf("\n================ %s ================\n", name);

    // ---- latency pass ----
    E e1;
    populate(e1, w);
    std::vector<std::uint64_t> buckets[NCAT];
    std::size_t lat_trades = 0;
    std::uint64_t lat_hash = latency_pass(e1, w, warmup, clock_med, buckets, lat_trades);

    std::printf("measured ops     : %zu  (warmup %zu excluded)\n",
                w.stream.size() - warmup, warmup);
    std::printf("latency (clock overhead already subtracted, floored at 0):\n");
    print_stats_header();
    std::vector<std::uint64_t> overall;
    overall.reserve(w.stream.size());
    for (int c = 0; c < NCAT; ++c) {
        for (auto v : buckets[c]) overall.push_back(v);
        Stats s = summarize(buckets[c]);   // sorts the bucket in place
        print_stats_row(cat_name((std::uint8_t)c), s);
    }
    Stats all = summarize(overall);
    print_stats_row("ALL", all);

    // ---- throughput pass (fresh book, identical stream) ----
    E e2;
    populate(e2, w);
    double mps = 0;
    std::size_t tp_trades = 0;
    std::uint64_t tp_hash = throughput_pass(e2, w, mps, tp_trades);

    std::printf("throughput       : %.3f M msgs/sec  (%.1f ns/msg amortized)\n",
                mps / 1e6, 1e9 / mps);
    std::printf("trades produced  : %zu\n", lat_trades);
    std::printf("trade checksum   : 0x%016llx  [latency vs throughput pass: %s]\n",
                (unsigned long long)lat_hash,
                (lat_hash == tp_hash && lat_trades == tp_trades) ? "MATCH" : "MISMATCH!!");
}

int main() {
    std::printf("=========================================================\n");
    std::printf(" Limit Order Book - Stage 0 baseline (naive map+deque)\n");
    std::printf("=========================================================\n");
    std::printf("build flags      : %s\n", BUILD_FLAGS);
    std::printf("compiler         : g++ %s\n", __VERSION__);
    std::printf("clock            : std::chrono::steady_clock\n");
    pin_cpu0();

    // Integrity rule #1: measure the measurement.
    ClockOverhead co = measure_clock_overhead();
    std::printf("clock overhead   : min=%llu  median=%llu  p99=%llu ns  (n=%zu)\n",
                (unsigned long long)co.min_ns, (unsigned long long)co.median_ns,
                (unsigned long long)co.p99_ns, co.samples);
    std::printf("  -> subtracting median (%llu ns) from every sample\n",
                (unsigned long long)co.median_ns);

    WorkloadParams p;             // all defaults: fixed seed, fixed sizes
    Workload w = make_workload(p);
    const std::size_t warmup = 100000;
    std::printf("\nworkload         : %s\n", w.description.c_str());
    std::printf("populate ops     : %zu (untimed)\n", w.populate.size());

    run_engine<NaiveOrderBook>("NaiveOrderBook (Stage 0)", w, warmup, co.median_ns);

    // Stage 1 will add: run_engine<FlatOrderBook>("FlatOrderBook (Stage 1)", w, warmup, co.median_ns);

    std::printf("\nNOTE: single-core container, CPU governor not visible -> absolute ns\n");
    std::printf("      are noisier than bare metal. The Stage 0 vs Stage 1 delta on this\n");
    std::printf("      identical replay is the trustworthy signal, not any one number.\n");
    return 0;
}
