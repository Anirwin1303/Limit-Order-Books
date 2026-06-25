//
// Correctness tests. Templated on the Engine concept so the SAME suite runs
// against Stage 0 and Stage 1 unchanged. Prove the engine is right before any
// number it produces is allowed to mean anything.
//
#include "types.hpp"
#include "naive_book.hpp"

#include <cstdio>
#include <vector>
#include <string>

using namespace ob;

// ----------------------------------------------------------------------------
// Tiny test framework: enough to count checks, report the first failing line of
// each test, and set a process exit code. No external dependency.
// ----------------------------------------------------------------------------
struct TestState {
    int checks = 0;
    int failures = 0;
    std::string current;
    int current_failures = 0;
};
static TestState g_state;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_state.checks;                                                      \
        if (!(cond)) {                                                         \
            ++g_state.failures;                                                \
            ++g_state.current_failures;                                        \
            std::printf("    [FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__,   \
                        #cond);                                                \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++g_state.checks;                                                      \
        auto _va = (a);                                                        \
        auto _vb = (b);                                                        \
        if (!(_va == _vb)) {                                                   \
            ++g_state.failures;                                                \
            ++g_state.current_failures;                                        \
            std::printf("    [FAIL] %s:%d  CHECK_EQ(%s, %s)  lhs=%lld rhs=%lld\n",\
                        __FILE__, __LINE__, #a, #b,                            \
                        (long long)_va, (long long)_vb);                       \
        }                                                                      \
    } while (0)

#define TEST(name)                                                             \
    g_state.current = name;                                                    \
    g_state.current_failures = 0;                                              \
    std::printf("  - %s\n", name);

#define END_TEST()                                                             \
    if (g_state.current_failures == 0)                                         \
        std::printf("    ok\n");

// Convenience: collect all trades from one submit into a fresh vector.
template <Engine E>
static std::vector<Trade> submit(E& book, const Op& op) {
    std::vector<Trade> out;
    book.submit(op, out);
    return out;
}

// ----------------------------------------------------------------------------
// The suite.
// ----------------------------------------------------------------------------
template <Engine E>
void run_all_tests(const char* engine_name) {
    std::printf("== %s ==\n", engine_name);

    {   TEST("passive add rests, no trade");
        E b;
        auto t = submit(b, Op::add(1, Side::Buy, 100, 5));
        CHECK_EQ(t.size(), 0u);
        CHECK(b.best_bid().has_value());
        CHECK_EQ(b.best_bid().value(), 100);
        CHECK_EQ(b.qty_at(Side::Buy, 100), 5u);
        CHECK_EQ(b.resting_count(), 1u);
        CHECK(!b.best_ask().has_value());
        END_TEST();
    }

    {   TEST("full match removes both");
        E b;
        submit(b, Op::add(1, Side::Sell, 100, 5));
        auto t = submit(b, Op::add(2, Side::Buy, 100, 5));
        CHECK_EQ(t.size(), 1u);
        CHECK_EQ(t[0].maker_id, 1u);
        CHECK_EQ(t[0].taker_id, 2u);
        CHECK_EQ(t[0].price, 100);
        CHECK_EQ(t[0].qty, 5u);
        CHECK_EQ(b.resting_count(), 0u);
        CHECK(!b.best_bid().has_value());
        CHECK(!b.best_ask().has_value());
        END_TEST();
    }

    {   TEST("incoming partially filled, remainder rests");
        E b;
        submit(b, Op::add(1, Side::Sell, 100, 3));
        auto t = submit(b, Op::add(2, Side::Buy, 100, 5));   // wants 5, only 3 available
        CHECK_EQ(t.size(), 1u);
        CHECK_EQ(t[0].qty, 3u);
        CHECK(!b.best_ask().has_value());                    // ask consumed
        CHECK_EQ(b.best_bid().value(), 100);                 // remainder of buy rests
        CHECK_EQ(b.qty_at(Side::Buy, 100), 2u);
        CHECK_EQ(b.resting_count(), 1u);
        END_TEST();
    }

    {   TEST("resting partially filled, stays on book");
        E b;
        submit(b, Op::add(1, Side::Sell, 100, 5));
        auto t = submit(b, Op::add(2, Side::Buy, 100, 2));   // takes 2 of the 5
        CHECK_EQ(t.size(), 1u);
        CHECK_EQ(t[0].qty, 2u);
        CHECK_EQ(b.best_ask().value(), 100);
        CHECK_EQ(b.qty_at(Side::Sell, 100), 3u);             // 3 remain resting
        CHECK_EQ(b.resting_count(), 1u);
        END_TEST();
    }

    {   TEST("FIFO time priority within a price level");
        E b;
        submit(b, Op::add(10, Side::Sell, 100, 2));          // arrives first
        submit(b, Op::add(11, Side::Sell, 100, 2));          // arrives second
        auto t = submit(b, Op::add(20, Side::Buy, 100, 3));  // sweeps 3
        CHECK_EQ(t.size(), 2u);
        CHECK_EQ(t[0].maker_id, 10u);                        // oldest filled first
        CHECK_EQ(t[0].qty, 2u);
        CHECK_EQ(t[1].maker_id, 11u);                        // then the next
        CHECK_EQ(t[1].qty, 1u);
        CHECK_EQ(b.qty_at(Side::Sell, 100), 1u);             // 1 left on order 11
        END_TEST();
    }

    {   TEST("multi-level sweep walks from best price inward");
        E b;
        submit(b, Op::add(1, Side::Sell, 100, 1));
        submit(b, Op::add(2, Side::Sell, 101, 1));
        auto t = submit(b, Op::add(3, Side::Buy, 101, 2));   // crosses both levels
        CHECK_EQ(t.size(), 2u);
        CHECK_EQ(t[0].maker_id, 1u);                         // best (lowest ask) first
        CHECK_EQ(t[0].price, 100);
        CHECK_EQ(t[1].maker_id, 2u);
        CHECK_EQ(t[1].price, 101);
        CHECK(!b.best_ask().has_value());
        END_TEST();
    }

    {   TEST("trade executes at MAKER price (aggressor gets price improvement)");
        E b;
        submit(b, Op::add(1, Side::Sell, 100, 1));           // resting ask at 100
        auto t = submit(b, Op::add(2, Side::Buy, 105, 1));   // willing to pay up to 105
        CHECK_EQ(t.size(), 1u);
        CHECK_EQ(t[0].price, 100);                           // fills at 100, NOT 105
        END_TEST();
    }

    {   TEST("non-crossing limit just rests (spread preserved)");
        E b;
        submit(b, Op::add(1, Side::Sell, 101, 1));
        auto t = submit(b, Op::add(2, Side::Buy, 100, 1));   // 100 < 101, no cross
        CHECK_EQ(t.size(), 0u);
        CHECK_EQ(b.best_bid().value(), 100);
        CHECK_EQ(b.best_ask().value(), 101);
        CHECK_EQ(b.resting_count(), 2u);
        END_TEST();
    }

    {   TEST("cancel removes a resting order so it cannot match");
        E b;
        submit(b, Op::add(1, Side::Sell, 100, 5));
        submit(b, Op::cancel(1));
        CHECK(!b.best_ask().has_value());
        CHECK_EQ(b.resting_count(), 0u);
        auto t = submit(b, Op::add(2, Side::Buy, 100, 5));   // nothing to match now
        CHECK_EQ(t.size(), 0u);
        CHECK_EQ(b.best_bid().value(), 100);                 // it rests instead
        END_TEST();
    }

    {   TEST("cancel unknown id is a no-op");
        E b;
        submit(b, Op::add(1, Side::Buy, 100, 5));
        submit(b, Op::cancel(999));                          // never existed
        submit(b, Op::cancel(1));
        submit(b, Op::cancel(1));                            // already gone
        CHECK_EQ(b.resting_count(), 0u);
        END_TEST();
    }

    {   TEST("cancel one of two FIFO orders; survivor still matches in order");
        E b;
        submit(b, Op::add(10, Side::Sell, 100, 2));
        submit(b, Op::add(11, Side::Sell, 100, 2));
        submit(b, Op::cancel(10));                           // remove the older one
        auto t = submit(b, Op::add(20, Side::Buy, 100, 2));
        CHECK_EQ(t.size(), 1u);
        CHECK_EQ(t[0].maker_id, 11u);                        // 11 is now the front
        CHECK_EQ(b.resting_count(), 0u);
        END_TEST();
    }

    {   TEST("top of book tracks best across multiple levels");
        E b;
        submit(b, Op::add(1, Side::Buy, 98, 1));
        submit(b, Op::add(2, Side::Buy, 100, 1));            // better bid
        submit(b, Op::add(3, Side::Buy, 99, 1));
        submit(b, Op::add(4, Side::Sell, 103, 1));
        submit(b, Op::add(5, Side::Sell, 101, 1));           // better ask
        submit(b, Op::add(6, Side::Sell, 102, 1));
        CHECK_EQ(b.best_bid().value(), 100);
        CHECK_EQ(b.best_ask().value(), 101);
        submit(b, Op::cancel(2));                            // drop the best bid
        CHECK_EQ(b.best_bid().value(), 99);                  // next best surfaces
        submit(b, Op::cancel(5));                            // drop the best ask
        CHECK_EQ(b.best_ask().value(), 102);
        END_TEST();
    }
}

int main() {
    run_all_tests<NaiveOrderBook>("NaiveOrderBook (Stage 0)");
    // Stage 1 will add: run_all_tests<FlatOrderBook>("FlatOrderBook (Stage 1)");

    std::printf("\n----------------------------------------\n");
    std::printf("checks: %d   failures: %d\n", g_state.checks, g_state.failures);
    if (g_state.failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("TESTS FAILED\n");
    return 1;
}
