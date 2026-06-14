// CCCC_FLAGS: -O1
// Tests for tail-call optimisation (CALLT emission).
// -O1 enables TCO without -O2 multi-statement inlining, which would transform
// mutual recursion before we get to test the CALLT path.

#include <stdio.h>

// --- Direct self-recursion (accumulator style) ---
// Stack depth must remain O(1); 500000 iterations would overflow without TCO.
static long tail_sum(long n, long acc) {
    if (n <= 0)
        return acc;
    return tail_sum(n - 1, acc + n);
}

// --- Mutual tail-recursion ---
// A and B alternate; 200000 calls each would overflow without TCO.
static int is_even(int n);
static int is_odd(int n);

static int is_even(int n) {
    if (n == 0) return 1;
    return is_odd(n - 1);
}
static int is_odd(int n) {
    if (n == 0) return 0;
    return is_even(n - 1);
}

// --- Non-tail call must NOT be optimised (correctness guard) ---
// return f() + 1 is not a tail call; the addition must still happen.
static int one(void) { return 1; }
static int non_tail(int n) {
    if (n <= 0) return 0;
    return non_tail(n - 1) + 1; // +1 makes it non-tail; result must equal n
}

// --- Nested arg: return f(g(x)) — exercises the flag-leak fix ---
// g must actually run; if its CALL is suppressed the result would be wrong.
static int double_it(int x) { return x * 2; }
static int add_one(int x)   { return x + 1; }
static int compose(int x)   { return add_one(double_it(x)); }

int main(void) {
    // Tail-recursive sum: 1+2+...+500000 = 125000250000
    long s = tail_sum(500000, 0);
    if (s != 125000250000L) {
        printf("FAIL tail_sum: got %ld\n", s);
        return 1;
    }

    // Mutual tail recursion over a large even number
    if (!is_even(200000)) {
        printf("FAIL is_even(200000)\n");
        return 2;
    }
    if (is_even(200001)) {
        printf("FAIL is_even(200001) should be odd\n");
        return 3;
    }

    // Non-tail call correctness: result must equal n
    if (non_tail(200) != 200) {
        printf("FAIL non_tail(200): got %d\n", non_tail(200));
        return 4;
    }

    // Nested arg: return f(g(x)) value correctness
    if (compose(5) != 11) { // (5*2)+1 = 11
        printf("FAIL compose(5): got %d\n", compose(5));
        return 5;
    }

    return 42;
}
