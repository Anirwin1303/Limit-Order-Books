<a id="readme-top"></a>

<!-- PROJECT HEADER -->
<br />
<div align="center">
  <h1 align="center">Limit Order Book &amp; Matching Engine</h1>

  <p align="center">
    A single-instrument, price-time-priority matching engine in C++20 — built
    naive, then flattened, and measured honestly against itself on a
    deterministic replay.
    <br />
    <a href="https://github.com/Anirwin1303/Limit-Order-Books"><strong>Explore the docs »</strong></a>
    <br />
    <br />
    <a href="https://github.com/Anirwin1303/Limit-Order-Books/issues">Report Bug</a>
    ·
    <a href="https://github.com/Anirwin1303/Limit-Order-Books/issues">Request Feature</a>
  </p>
</div>

<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
      <ul><li><a href="#built-with">Built With</a></li></ul>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#installation">Installation</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#how-it-works">How It Works</a></li>
    <li><a href="#the-engines">The Engines</a></li>
    <li><a href="#results">Results</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#contact">Contact</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>

<!-- ABOUT THE PROJECT -->
## About The Project

A limit order book is the central data structure of every exchange, and a
matching engine is its heart. The matching rules are simple; the engineering
interest is that the data-structure choices have order-of-magnitude consequences
for latency — and those consequences are measurable.

This project builds a single-instrument, price-time-priority matching engine
**three times** behind one shared interface, then benchmarks the versions
against each other on a **byte-identical deterministic replay**. A naive
`std::map` + `std::deque` engine establishes a correct baseline; a flattened
rewrite replaces the price tree with a direct-indexed array; and a final version
adds a per-level intrusive linked list drawn from a preallocated node pool for
O(1) cancels and zero hot-path allocation.

The deliverable is the **comparison**, not the engine. Correctness is proven
first (55 checks per engine), and all three versions emit the identical trade
stream (checksum `0xb1d511ede4054b89`), so every latency difference below is a
pure performance change — never a behavior change.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

### Built With

* [![C++][cpp-shield]][cpp-url] — the engines and harness (C++20 concepts as the shared interface)
* [![GNU Make][make-shield]][make-url] — one-command build for tests and benchmark
* [![GCC][gcc-shield]][gcc-url] — `g++ 13.3`, compiled at `-O3 -DNDEBUG`
* [![Linux][linux-shield]][linux-url] — CPU pinning via `sched_setaffinity`; `perf` for profiling
* `std::chrono::steady_clock` for timing, with measured clock-overhead correction

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- GETTING STARTED -->
## Getting Started

### Prerequisites

* **A C++20 compiler** — GCC 13+ or Clang 16+ (developed on `g++ 13.3`).
* **GNU Make.**
* **Linux** — the harness uses `sched_setaffinity` for CPU pinning and `perf`
  for the Stage 2 flamegraph.

### Installation

1. Clone the repo
   ```sh
   git clone https://github.com/Anirwin1303/limit-order-book.git
   cd limit-order-book
   ```
2. Run the correctness suite (165 checks across all three engines)
   ```sh
   make test
   ```
3. Run the benchmark comparison
   ```sh
   make bench
   ```
4. Optionally target your host CPU (keep it consistent across all engines)
   ```sh
   make bench MARCH=-march=native
   ```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- USAGE -->
## Usage

`make bench` builds and runs the harness, printing the clock overhead, a
per-operation latency table for each engine, throughput, and the trade checksum:

```text
clock overhead   : min=18  median=20  p99=22 ns  (n=200000)

================ FlatOrderBook (Stage 1b: flat + intrusive + pool) ================
  category              count     min     mean     p50     p90     p99    p99.9       max
  add_passive          450182       6     66.2      38     153     282      558     59717
  add_marketable       250382       4     87.3      50     167     380      695     75625
  cancel               299436      19    194.2     157     281     492     3317     87486
  ALL                 1000000       4    109.8      66     198     410      738     87486
throughput       : 9.371 M msgs/sec  (106.7 ns/msg amortized)
trade checksum   : 0xb1d511ede4054b89  [latency vs throughput pass: MATCH]
```

The workload is a fixed-seed replay: the same seed produces the same order
stream and the same trades, every run and across every engine. Change the seed,
size, or op mix in `bench/workload.hpp`.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- HOW IT WORKS -->
## How It Works

1. **Model.** Two sorted sides of resting limit orders — bids descending by
   price, asks ascending — with strict FIFO time priority within each price
   level. Prices are integer ticks (never floats, which can't represent most
   decimal prices exactly).
2. **Match.** An incoming buy crosses if its limit is at or above the best ask
   (a sell is symmetric). On a cross, the engine fills against resting orders
   from the best price inward, and within a level from the oldest order forward.
3. **Price improvement.** Each trade executes at the **resting (maker) order's
   price**, so the aggressor receives any price improvement — getting this wrong
   is a subtly broken exchange, so it has its own test.
4. **Rest & cancel.** Whatever quantity doesn't fill rests on the book; a cancel
   removes a resting order by id. Partial fills are the norm.
5. **Optimize & measure.** Build the naive engine first and prove it correct,
   then flatten the data structures one isolated change at a time — predicting
   each speedup, measuring it on an identical replay, and explaining any miss.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- THE ENGINES -->
## The Engines

| Component | File | Role |
| --- | --- | --- |
| **Stage 0 — Naive** | `include/naive_book.hpp` | `std::map` price levels + `std::deque` per level. Correct baseline; cache-hostile and allocation-heavy. |
| **Stage 1a — Flat array** | `include/flat_book.hpp` (`FlatDequeBook`) | Price tree replaced by a direct-indexed array; deque per level kept. Isolates the cache/traversal win. |
| **Stage 1b — Flat + pool** | `include/flat_book.hpp` (`FlatOrderBook`) | Per-level intrusive linked list from a preallocated node pool. O(1) cancel, zero hot-path allocation. |
| **Harness** | `bench/latency.cpp` | Clock-overhead-corrected p50/p99/p99.9 latency + throughput over the replay. |
| **Workload** | `bench/workload.hpp` | Deterministic seeded generator: a mix of passive adds, marketable adds, and cancels. |
| **Tests** | `tests/test_main.cpp` | 55 checks per engine (165 total): FIFO priority, partial fills, multi-level sweep, cancel, maker-price. |

`include/types.hpp` defines the shared `Engine` **concept** that all three
engines satisfy, so the tests and harness run against each one unchanged, with no
virtual dispatch on the hot path.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- RESULTS -->
## Results

Per-operation latency in **nanoseconds**, clock overhead subtracted, 100k warmup
excluded, identical 1,000,000-op replay. Ratios are stable run-to-run; absolute
ns jitter ~5–15% in a single-core container.

| metric            | Stage 0 | 1a (flat array) | 1b (flat + pool) | 0 → 1b |
|-------------------|--------:|----------------:|-----------------:|-------:|
| ALL p50           |     190 |             111 |               66 | 2.9×   |
| ALL p99           |     733 |             598 |              410 | 1.8×   |
| **ALL p99.9**     |   3,933 |           1,695 |              738 | **5.3×** |
| cancel p50        |     420 |             309 |              157 | 2.7×   |
| add_passive p99.9 |   3,622 |           1,607 |              558 | 6.5×   |
| **throughput** (M msgs/sec) | 4.01 | 6.12 |             9.37 | **2.3×** |

**Why each change moved the numbers:** flattening the tree into an array cut the
medians (a pointer-chased traversal became one index) and, surprisingly, also cut
the *add* tails — Stage 0 was allocating a red-black-tree node on every new price
level, which a preallocated array eliminates before any pooling. The intrusive
list + pool then collapsed `cancel` (O(1) unlink replacing a deque scan-and-shift)
and compressed the remaining tails to near-zero hot-path allocation.

**Reproducibility:** fixed seed `0x9E3779B97F4A7C15`; flags `-std=c++20 -O3
-DNDEBUG`; trade checksum `0xb1d511ede4054b89` / 266,228 trades identical across
runs, across the latency vs throughput passes, and across all three engines.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- ROADMAP -->
## Roadmap

- [x] Stage 0 — naive `map`+`deque` engine + 55-check correctness suite
- [x] Latency harness — clock-overhead correction, p50/p99/p99.9, deterministic replay
- [x] Stage 1a — flat tick-indexed price array
- [x] Stage 1b — intrusive list + preallocated node pool
- [ ] Stage 2 — `perf`/flamegraph; replace the `unordered_map` id index; best-price repair scan
- [ ] `rdtsc`-based timing with serialization (once the `chrono` harness is trusted)
- [ ] NASDAQ ITCH feed handler (real binary market-data protocol)
- [ ] Wire in a lock-free SPSC ring buffer for order entry

See the [open issues](https://github.com/Anirwin1303/limit-order-book/issues) for
a full list of proposed features and known issues.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- CONTACT -->
## Contact

Anirvinna Jain - anirwin2003@gmail.com

Project Link: [https://github.com/Anirwin1303/Limit-Order-Books](https://github.com/Anirwin1303/Limit-Order-Books)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- ACKNOWLEDGMENTS -->
## Acknowledgments

* [What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf) — Ulrich Drepper, the basis for the cache-locality reasoning.
* [FlameGraph](https://github.com/brendangregg/FlameGraph) — by Brendan Gregg, for the Stage 2 profiling.
* [NASDAQ TotalView-ITCH specification](https://www.nasdaqtrader.com/Trader.aspx?id=ITCH) — the real market-data protocol targeted as a stretch goal.
* [Best-README-Template](https://github.com/othneildrew/Best-README-Template) — by othneildrew, used as the basis for this README.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- MARKDOWN LINKS & IMAGES -->
[cpp-shield]: https://img.shields.io/badge/C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white
[cpp-url]: https://en.cppreference.com/w/cpp/20
[make-shield]: https://img.shields.io/badge/GNU%20Make-A42E2B?style=for-the-badge&logo=gnu&logoColor=white
[make-url]: https://www.gnu.org/software/make/
[gcc-shield]: https://img.shields.io/badge/GCC-4EAA25?style=for-the-badge&logo=gnu&logoColor=white
[gcc-url]: https://gcc.gnu.org/
[linux-shield]: https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black
[linux-url]: https://kernel.org/
