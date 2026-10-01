// CCCC_FLAGS: --testing
// Suite: [[cccc::global]]/local/constant/private/generic address spaces on
// pointers in [[cccc::kernel]] code. Negative cases live in
// tests/test_kernel_addr_space_*_error.c.

#include <stdatomic.h>

#if !__has_c_attribute(cccc::global) || !__has_c_attribute(cccc::generic)
#error expected __has_c_attribute for the address-space attributes
#endif

typedef struct {
    int n;
    int v;
} Params;

typedef [[cccc::global]] int  GInt;

static const int              table[4] = {10, 20, 30, 40};
[[cccc::constant]] static int scale[2] = {2, 3};

static inline unsigned bump(_Atomic unsigned *p) {
    return atomic_fetch_add_explicit(p, 1u, memory_order_relaxed);
}

static inline int load_at([[cccc::constant]] const Params *prm, int i) {
    return prm->v + table[i & 3] + scale[i & 1];
}

static inline int helper_second(int *p) {
    return p[0] * 2;
}

static inline int twice(int *p) {
    return helper_second(p);
}

#pragma cccc suite begin "kernel_addr_space"

[[cccc::kernel]] static void
step(int *out, [[cccc::constant]] const Params *prm, unsigned i) {
    out[i]  = load_at(prm, (int)i);
    out[i] += 1;
    out[i]++;
    [[cccc::global]] int *g  = out;
    g[0]                    += prm->n;
}

// The documented example: an entry forwards a buffer to an unmarked helper.
// Compile-only, calling it crashes the VM (#1393).
[[cccc::kernel]] static void docs_example(_Atomic unsigned *counters,
                                          unsigned          i) {
    bump(&counters[i]);
}

[[cccc::test(return = 42)]]
int test_entry_params_infer_global(void) {
    int    out[2] = {0, 0};
    Params prm    = {1, 5};
    step(out, &prm, 1);
    return out[1] == 5 + 20 + 3 + 2 ? 42 : 1;
}

[[cccc::kernel]] static int via_helper(int *p, int i) {
    return twice(p + i);
}

[[cccc::test(return = 42)]]
int test_helper_param_inferred_from_caller(void) {
    int v[2] = {1, 21};
    return via_helper(v, 1);
}

[[cccc::kernel]] static void tile_sum([[cccc::global]] int *o,
                                      [[cccc::local]] int  *scratch) {
    [[cccc::local]] int tile[4];
    tile[0]    = 40;
    scratch[0] = 2;
    o[0]       = tile[0] + scratch[0];
}

[[cccc::test(return = 42)]]
int test_local_tile_and_local_param(void) {
    int out = 0;
    int scratch[1];
    tile_sum(&out, scratch);
    return out;
}

static inline int read_any([[cccc::generic]] const int *p) {
    return p[0];
}

[[cccc::kernel]] static int mixed_spaces([[cccc::global]] const int *g,
                                         [[cccc::local]] const int  *l) {
    return read_any(g) + read_any(l);
}

[[cccc::test(return = 42)]]
int test_generic_accepts_global_and_local(void) {
    int a = 40, b = 2;
    return mixed_spaces(&a, &b);
}

[[cccc::kernel]] static int generic_back([[cccc::global]] int *g) {
    [[cccc::generic]] int *x = g;
    [[cccc::global]] int  *y = (GInt *)x;
    return y[0];
}

[[cccc::test(return = 42)]]
int test_explicit_cast_from_generic(void) {
    int v = 42;
    return generic_back(&v);
}

[[cccc::test(return = 42)]]
int test_host_call_with_plain_pointer(void) {
    int              v[2]   = {0, 0};
    int              tmp[2] = {1, 0};
    _Atomic unsigned c[2]   = {0, 0};
    Params           prm    = {0, 0};
    step(v, &prm, 0);
    return tmp[0] == 1 && v[0] == 14 ? 42 : 1;
}

#pragma cccc suite end
