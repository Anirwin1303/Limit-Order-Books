<a id="readme-top"></a>

<!--
  Placeholders to replace before publishing:
    your_username        -> your GitHub username
    repo_name            -> the repository name
    Your Name / email / socials in the Contact section
  Images to add under images/ :  logo.png, latency.png (or flamegraph.svg)
-->

<!-- PROJECT SHIELDS -->
<div align="center">

[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![MIT License][license-shield]][license-url]
[![LinkedIn][linkedin-shield]][linkedin-url]

</div>

<!-- PROJECT LOGO -->
<br />
<div align="center">
  <a href="https://github.com/your_username/repo_name">
    <img src="images/logo.png" alt="Logo" width="80" height="80">
  </a>

  <h3 align="center">Limit Order Book &amp; Matching Engine</h3>

  <p align="center">
    A cache-optimized, price-time-priority matching engine in C++20 — built naive,
    then flattened, and measured honestly against itself.
    <br />
    <a href="#results"><strong>Explore the benchmarks »</strong></a>
    <br />
    <br />
    <a href="#usage">View Demo</a>
    &middot;
    <a href="https://github.com/your_username/repo_name/issues/new?labels=bug&template=bug-report---.md">Report Bug</a>
    &middot;
    <a href="https://github.com/your_username/repo_name/issues/new?labels=enhancement&template=feature-request---.md">Request Feature</a>
  </p>
</div>

<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
      <ul>
        <li><a href="#built-with">Built With</a></li>
      </ul>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#installation">Installation</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#results">Results</a></li>
    <li><a href="#design-notes">Design Notes</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>

<!-- ABOUT THE PROJECT -->
## About The Project

<!-- Add a screenshot of your flamegraph or latency plot here -->
<!-- [![Latency Screenshot][product-screenshot]](https://example.com) -->

A limit order book is the central data structure of every exchange, and a matching
engine is its heart. The matching rules are simple; the engineering interest is
that the data-structure choices have order-of-magnitude consequences for latency —
and those consequences are measurable.

This project builds a single-instrument, price-time-priority matching engine
**three times** behind one shared interface, and benchmarks the versions against
each other on a **byte-identical deterministic replay**:

* **Stage 0 — `NaiveOrderBook`:** `std::map` price levels + `std::deque` per level. Correct, but cache-hostile and allocation-heavy.
* **Stage 1a — `FlatDequeBook`:** the price tree replaced by a direct-indexed array. Isolates the cache/traversal win.
* **Stage 1b — `FlatOrderBook`:** adds a per-level intrusive doubly-linked list drawn from a preallocated node pool. O(1) cancel, zero hot-path allocation.

The deliverable is the **comparison**, not the engine. All three versions emit the
identical trade stream (checksum `0xb1d511ede4054b89`), so every latency difference
is a pure performance change — never a behavior change.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

### Built With

* [![C++][cpp-shield]][cpp-url]
* [![GNU Make][make-shield]][make-url]
* [![GCC][gcc-shield]][gcc-url]
* [![Linux][linux-shield]][linux-url]
* Linux `perf` + FlameGraph (Stage 2 profiling)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- GETTING STARTED -->
## Getting Started

To get a local copy up and running follow these steps.

### Prerequisites

* A C++20 compiler — GCC 13+ or Clang 16+ (developed on `g++ 13.3`)
* GNU Make
* Linux (the harness uses `sched_setaffinity` for CPU pinning and `perf` for profiling)

### Installation

1. Clone the repo
   ```sh
   git clone https://github.com/your_username/repo_name.git
   cd repo_name
   ```
2. Run the correctness suite (165 checks across all three engines)
   ```sh
   make test
   ```
3. Run the benchmark comparison
   ```sh
   make bench
   ```
   Optionally target your host CPU (keep it consistent across engines):
   ```sh
   make bench MARCH=-march=native
   ```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- USAGE -->
## Usage

`make bench` builds and runs the harness, printing clock overhead, a per-operation
latency table for each engine, throughput, and the trade checksum:

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

The workload is a fixed-seed replay: same seed → same order stream → same trades,
every run and across every engine. Change the seed, size, or op mix in
`bench/workload.hpp`.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- RESULTS -->
## Results

Per-operation latency in **nanoseconds**, clock overhead subtracted, 100k warmup
excluded, identical 1,000,000-op replay. Ratios are stable run-to-run; absolute ns
jitter ~5–15% in a single-core container.

| metric            | Stage 0 | 1a (flat array) | 1b (flat + intrusive + pool) | 0 → 1b |
|-------------------|--------:|----------------:|-----------------------------:|-------:|
| ALL p50           |     190 |             111 |                           66 | 2.9×   |
| ALL p99           |     733 |             598 |                          410 | 1.8×   |
| **ALL p99.9**     |   3,933 |           1,695 |                          738 | **5.3×** |
| cancel p50        |     420 |             309 |                          157 | 2.7×   |
| add_passive p99.9 |   3,622 |           1,607 |                          558 | 6.5×   |
| **throughput** (M msgs/sec) | 4.01 | 6.12 |                     9.37 | **2.3×** |

**Reproducibility:** fixed seed `0x9E3779B97F4A7C15`; flags `-std=c++20 -O3 -DNDEBUG`;
trade checksum `0xb1d511ede4054b89` / 266,228 trades identical across runs, across the
latency vs throughput passes, and across all three engines.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- DESIGN NOTES -->
## Design Notes

Why each change moved the numbers — decomposed so each win is attributable:

* **Flat array (1a) cut the medians** (add p50 165 → 69 ns): the `std::map`'s
  pointer-chased tree traversal per operation became a single array index. Pure
  cache/traversal win.
* **A surprise in the tails:** flattening also cut the *add* p99.9 by ~2–3×, before
  any pooling — because Stage 0 was allocating a red-black-tree node on every new
  price level, which a preallocated array eliminates. "Zero hot-path allocation"
  turned out to be two allocation sources (tree nodes, then deque growth) removed
  in two steps.
* **Intrusive list + pool (1b) collapsed `cancel`** (420 → 157 ns p50): an O(1)
  unlink replaced the deque scan-and-shift. Pure algorithmic win. Cancel is now
  bound by the `unordered_map` id index — the next target.

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
- [ ] Wire in the lock-free SPSC ring buffer

See the [open issues](https://github.com/your_username/repo_name/issues) for a full list of proposed features.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- CONTRIBUTING -->
## Contributing

This is a portfolio project, but suggestions and bug reports are welcome. If you
have an improvement, fork the repo and open a pull request, or open an issue with
the tag "enhancement".

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- LICENSE -->
## License

Distributed under the MIT License. See `LICENSE.txt` for more information.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- CONTACT -->
## Contact

Your Name - [@your_twitter](https://twitter.com/your_username) - email@example.com

Project Link: [https://github.com/your_username/repo_name](https://github.com/your_username/repo_name)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- ACKNOWLEDGMENTS -->
## Acknowledgments

* [Best-README-Template](https://github.com/othneildrew/Best-README-Template) — the structure of this README
* [What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf) — Ulrich Drepper
* [FlameGraph](https://github.com/brendangregg/FlameGraph) — Brendan Gregg
* [NASDAQ TotalView-ITCH specification](https://www.nasdaqtrader.com/Trader.aspx?id=ITCH)
* [Img Shields](https://shields.io)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- MARKDOWN LINKS & IMAGES -->
[contributors-shield]: https://img.shields.io/github/contributors/your_username/repo_name.svg?style=for-the-badge
[contributors-url]: https://github.com/your_username/repo_name/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/your_username/repo_name.svg?style=for-the-badge
[forks-url]: https://github.com/your_username/repo_name/network/members
[stars-shield]: https://img.shields.io/github/stars/your_username/repo_name.svg?style=for-the-badge
[stars-url]: https://github.com/your_username/repo_name/stargazers
[issues-shield]: https://img.shields.io/github/issues/your_username/repo_name.svg?style=for-the-badge
[issues-url]: https://github.com/your_username/repo_name/issues
[license-shield]: https://img.shields.io/github/license/your_username/repo_name.svg?style=for-the-badge
[license-url]: https://github.com/your_username/repo_name/blob/master/LICENSE.txt
[linkedin-shield]: https://img.shields.io/badge/-LinkedIn-black.svg?style=for-the-badge&logo=linkedin&colorB=555
[linkedin-url]: https://linkedin.com/in/your_username
[product-screenshot]: images/latency.png
[cpp-shield]: https://img.shields.io/badge/C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white
[cpp-url]: https://en.cppreference.com/w/cpp/20
[make-shield]: https://img.shields.io/badge/GNU%20Make-A42E2B?style=for-the-badge&logo=gnu&logoColor=white
[make-url]: https://www.gnu.org/software/make/
[gcc-shield]: https://img.shields.io/badge/GCC-4EAA25?style=for-the-badge&logo=gnu&logoColor=white
[gcc-url]: https://gcc.gnu.org/
[linux-shield]: https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black
[linux-url]: https://kernel.org/
