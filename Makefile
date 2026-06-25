# Consistent, reproducible flags. The SAME flags must be used for Stage 0 and
# Stage 1 or the comparison is meaningless.
CXX      ?= g++
STD       = -std=c++20
WARN      = -Wall -Wextra
INCLUDE   = -Iinclude

# Tests: optimized but assertions LIVE (no -DNDEBUG).
TEST_FLAGS  = $(STD) -O2 $(WARN) $(INCLUDE)
# Bench: -O3, assertions off. (-march=native is a documented option; left off by
# default so numbers are portable. Turn on with `make bench MARCH=-march=native`.)
MARCH       ?=
BENCH_OPT    = -O3 -DNDEBUG $(MARCH)
BENCH_FLAGS  = $(STD) $(BENCH_OPT) $(WARN) $(INCLUDE) -Ibench \
               -DBUILD_FLAGS='"$(STD) $(BENCH_OPT)"'

.PHONY: all test bench clean
all: test bench

test:
	$(CXX) $(TEST_FLAGS) test_main.cpp -o build_test
	./build_test

bench:
	$(CXX) $(BENCH_FLAGS) latency.cpp -o build_bench
	./build_bench

clean:
	rm -f build_test build_bench
