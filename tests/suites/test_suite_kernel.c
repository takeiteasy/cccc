// CCCC_FLAGS: --testing
// Suite: [[cccc::kernel]] accepts the GPU-safe subset: stdatomic relaxed ops,
// static inline helper DAGs, structs of pointers by value, local arrays,
// bounded loops and const tables. Negative cases live in
// tests/test_kernel_*_error.c.

#include <stdatomic.h>
#include <stdint.h>

#if !__has_c_attribute(cccc::kernel)
#error expected __has_c_attribute(cccc::kernel)
#endif

typedef _Atomic uint32_t AU32;
typedef struct {
    AU32           *cells;
    const uint32_t *book;
    uint32_t        cap;
} Net;

#pragma cccc suite begin "kernel"

static const uint32_t table[4] = {1, 2, 3, 4};
enum { K_ADD = 3 };
#define K_MASK 0x0FFFFFFFu

static inline uint32_t push(Net n, uint32_t v) {
    uint32_t i =
        atomic_fetch_add_explicit(&n.cells[0], 1u, memory_order_relaxed);
    if (i >= n.cap)
        return 0;
    atomic_store_explicit(&n.cells[i + 1], v & K_MASK, memory_order_relaxed);
    return i;
}

static inline uint32_t link(Net n, uint32_t a) {
    for (uint32_t steps = 0;; steps++) {
        if (steps > n.cap)
            return 0;
        uint32_t old =
            atomic_exchange_explicit(&n.cells[1], a, memory_order_relaxed);
        if (old == 0)
            return push(n, a + K_ADD);
        a = old + table[steps & 3u];
    }
}

[[cccc::kernel]] static inline void step(Net n, uint32_t i) {
    uint32_t xs[2] = {i, i + 1};
    uint32_t ys[2];
    for (uint32_t k = 0; k < 2; k++)
        ys[k] = link(n, xs[k]);
    atomic_store_explicit(&n.cells[0], ys[0] + ys[1] > 0 ? 1u : 0u,
                          memory_order_relaxed);
}

static AU32 cells[16];

[[cccc::test(return = 42)]]
int test_kernel_accepts_subset(void) {
    Net n = {cells, table, 14};
    step(n, 1);
    return 42;
}

static int helper_after(int x);
[[cccc::kernel]] static int later_defined(int x) {
    return helper_after(x);
}
static int helper_after(int x) {
    return x + 1;
}

[[cccc::test(return = 42)]]
int test_kernel_callee_defined_later(void) {
    return later_defined(41);
}

#pragma cccc suite end
