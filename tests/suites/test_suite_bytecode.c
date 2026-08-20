// CCCC_FLAGS: --testing
// Consolidated suite: bytecode round-trip, C4 ABI, include routing
// Source tests: test_bytecode_32bit_roundtrip, test_c4_abi_rehydrate,
// test_include_route_build_skip

#include "stdarg.h"
#include[[cccc::build]] "fixtures/build_only.h"

// [from test_bytecode_32bit_roundtrip]
static int callee(int x) {
    return x + 1;
}

int (*global_fp)(int) = callee;

// [from test_c4_abi_rehydrate]
// Regression coverage for .c4 ABI rehydration.
// Expected return: 42

struct Pair {
    int a;
    int b;
};

union Word {
    int i;
};

static struct Pair make_pair(int a, int b) {
    struct Pair p;
    p.a = a;
    p.b = b;
    return p;
}

static int pair_sum(struct Pair p) {
    return p.a + p.b;
}

static union Word make_word(int value) {
    union Word w;
    w.i = value;
    return w;
}

static struct Pair sum_var_pairs(int count, ...) {
    va_list ap;
    va_start(ap, count);

    struct Pair p;
    p.a = 0;
    p.b = 0;
    for (int i = 0; i < count; i++) {
        p.a += va_arg(ap, int);
        p.b += va_arg(ap, int);
    }

    va_end(ap);
    return p;
}

static int *static_array(void) {
    static int values[3];
    values[0] = 10;
    values[1] = 20;
    values[2] = 12;
    return values;
}

int globals[2];

static int *global_array(void) {
    return globals;
}

// [from test_include_route_build_skip]
// Tests that #include [[cccc::build]] is skipped in normal compilation mode.

#pragma cccc suite begin "bytecode"

// test_bytecode_32bit_roundtrip
[[cccc::test(return = 42)]]
int test_bytecode_32bit_roundtrip(void) {
    long long wide = 0x100000000LL + 40;
    if ((int)(wide - 0x100000000LL) != 40)
        return 1;

    int (*local_fp)(int) = callee;
    if (local_fp(41) != 42)
        return 2;
    if (global_fp(41) != 42)
        return 3;

    goto *&&done;
    return 4;

done:
    return 42;
}

// test_c4_abi_rehydrate
[[cccc::test(return = 42)]]
int test_c4_abi_rehydrate(void) {
    struct Pair p = make_pair(10, 32);
    if (pair_sum(p) != 42)
        return 1;

    union Word w = make_word(42);
    if (w.i != 42)
        return 2;

    struct Pair vp = sum_var_pairs(2, 5, 7, 11, 19);
    if (vp.a != 16)
        return 3;
    if (vp.b != 26)
        return 4;

    int *s = static_array();
    if (s[0] + s[1] + s[2] != 42)
        return 5;

    int *g = global_array();
    g[0]   = 21;
    g[1]   = 21;
    if (globals[0] + globals[1] != 42)
        return 6;

    return 42;
}

// test_include_route_build_skip
[[cccc::test(return = 42)]]
int test_include_route_build_skip(void) {
#ifdef BUILD_ONLY_LOADED
    return 1;
#endif
    return 42;
}

#pragma cccc suite end
