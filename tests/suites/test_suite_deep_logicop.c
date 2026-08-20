// CCCC_FLAGS: --testing
// Regression for register exhaustion in deeply nested && and || chains.
// Before the fix, ND_LOGAND/ND_LOGOR each allocated a separate r_cond temp
// that stayed live while gen_cond_expr recursed into the operands — O(depth)
// register pressure. At depth 12 (> the 11-entry temp pool) compilation
// aborted with "out of temporary registers". Fix: reuse dest_reg as the
// condition scratch, matching the spill-path approach from ticket #587.
#include <stdbool.h>

// 15-deep && chain (well past the old depth-11 limit)
[[cccc::test]]
void test_logand_deep(void) {
    int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8, i = 9, j = 10,
        k = 11, l = 12, m = 13, n = 14, o = 15;
    int r =
        (a > 0 && b > 0 && c > 0 && d > 0 && e > 0 && f > 0 && g > 0 && h > 0 &&
         i > 0 && j > 0 && k > 0 && l > 0 && m > 0 && n > 0 && o > 0);
    AssertEq(r, 1);
    int r2 =
        (a > 0 && b > 0 && c > 0 && d > 0 && e > 0 && f > 0 && g > 0 && h > 0 &&
         i > 0 && j > 0 && k > 0 && l > 0 && m > 0 && n > 99 && o > 0);
    AssertEq(r2, 0);
}

// 15-deep || chain
[[cccc::test]]
void test_logor_deep(void) {
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, i = 0, j = 0,
        k = 0, l = 0, m = 0, n = 0, o = 1;
    int r = (a || b || c || d || e || f || g || h || i || j || k || l || m ||
             n || o);
    AssertEq(r, 1);
    int r2 = (a || b || c || d || e || f || g || h || i || j || k || l || m ||
              n || 0);
    AssertEq(r2, 0);
}

// Mixed && and || nesting — exercises both paths interleaved
[[cccc::test]]
void test_logic_mixed_deep(void) {
    int v = 5;
    int r =
        (v > 0 &&
         (v < 10 ||
          (v != 3 &&
           (v != 4 ||
            (v == 5 &&
             (v >= 5 ||
              (v <= 5 &&
               (v != 0 ||
                (v != 1 && (v != 2 || (v > -1 && (v < 100 || v == 5))))))))))));
    AssertEq(r, 1);
}

// Ternary with deeply nested condition (ND_COND also reuses dest_reg now)
[[cccc::test]]
void test_ternary_deep_cond(void) {
    int x = 3;
    int r =
        (x > 0 && x < 10 && x != 1 && x != 2 && x != 4 && x != 5 && x != 6 &&
         x != 7 && x != 8 && x != 9 && x != 0 && x != -1 && x != -2)
            ? 99
            : 0;
    AssertEq(r, 99);
}

// Short-circuit: && must stop at first false
[[cccc::test]]
void test_logand_short_circuit(void) {
    int calls = 0;
    // Use a statement-expression to count calls
    int r = (0 && ({
                 calls++;
                 1;
             }) &&
             ({
                 calls++;
                 1;
             }));
    AssertEq(r, 0);
    AssertEq(calls, 0); // short-circuited: nothing after the 0 executed
}

// Short-circuit: || must stop at first true
[[cccc::test]]
void test_logor_short_circuit(void) {
    int calls = 0;
    int r     = (1 || ({
                 calls++;
                 0;
                 }) ||
                 ({
                 calls++;
                 0;
                 }));
    AssertEq(r, 1);
    AssertEq(calls, 0);
}
