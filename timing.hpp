#pragma once
//
// Timing + statistics primitives for the latency harness.
//
// Integrity rule #1 (from the brief): measure the cost of the measurement
// itself. Every per-op sample includes the overhead of reading the clock; if
// you don't subtract it you are reporting the clock's latency, not the engine's.
// `measure_clock_overhead` reads the clock back-to-back with nothing in between
// and reports that floor so we can subtract it.
//
// Integrity rule #2: report PERCENTILES, not averages. Tail latency is the game
// in this industry. We compute p50/p90/p99/p99.9 (mean is shown only as a
// sanity column, never as the headline).
//
#include <cstdint>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstddef>

namespace bench {

// steady_clock is monotonic and is the trusted baseline clock per the brief
// (rdtsc with serialization is a documented stretch goal, only once this is
// trusted). On Linux this lowers to clock_gettime(CLOCK_MONOTONIC).
inline std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Compiler barriers so the optimizer cannot delete the work we are timing.
template <class T>
inline void do_not_optimize(const T& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}
inline void clobber() { asm volatile("" : : : "memory"); }

struct ClockOverhead {
    std::uint64_t min_ns;
    std::uint64_t median_ns;   // <- the value we subtract from each sample
    std::uint64_t p99_ns;
    std::size_t   samples;
};

inline ClockOverhead measure_clock_overhead(std::size_t n = 200000) {
    std::vector<std::uint64_t> d;
    d.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::uint64_t a = now_ns();
        std::uint64_t b = now_ns();
        d.push_back(b - a);
    }
    std::sort(d.begin(), d.end());
    return ClockOverhead{
        d.front(),
        d[d.size() / 2],
        d[static_cast<std::size_t>(d.size() * 0.99)],
        n};
}

struct Stats {
    std::size_t   count = 0;
    std::uint64_t min = 0, max = 0, p50 = 0, p90 = 0, p99 = 0, p999 = 0;
    double        mean = 0.0;
};

// Nearest-rank percentile on an already-sorted vector.
inline std::uint64_t pct(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    double idx = p * static_cast<double>(sorted.size() - 1);
    std::size_t i = static_cast<std::size_t>(idx + 0.5);
    if (i >= sorted.size()) i = sorted.size() - 1;
    return sorted[i];
}

// Sorts `s` in place and summarizes it.
inline Stats summarize(std::vector<std::uint64_t>& s) {
    Stats st;
    st.count = s.size();
    if (s.empty()) return st;
    std::sort(s.begin(), s.end());
    long double sum = 0;
    for (auto v : s) sum += v;
    st.mean = static_cast<double>(sum / static_cast<long double>(s.size()));
    st.min  = s.front();
    st.max  = s.back();
    st.p50  = pct(s, 0.50);
    st.p90  = pct(s, 0.90);
    st.p99  = pct(s, 0.99);
    st.p999 = pct(s, 0.999);
    return st;
}

inline void print_stats_header() {
    std::printf("  %-16s %10s %7s %8s %7s %7s %7s %8s %9s\n", "category",
                "count", "min", "mean", "p50", "p90", "p99", "p99.9", "max");
    std::printf("  %-16s %10s %7s %8s %7s %7s %7s %8s %9s\n", "", "", "(ns)",
                "(ns)", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)");
}
inline void print_stats_row(const char* name, const Stats& s) {
    std::printf("  %-16s %10zu %7llu %8.1f %7llu %7llu %7llu %8llu %9llu\n", name,
                s.count, (unsigned long long)s.min, s.mean,
                (unsigned long long)s.p50, (unsigned long long)s.p90,
                (unsigned long long)s.p99, (unsigned long long)s.p999,
                (unsigned long long)s.max);
}

} // namespace bench
