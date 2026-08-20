// CCCC_FLAGS: -3
// Ticket #740: the actual bug as filed (distinct from the local
// struct/union member-access FP fixed by addr_is_local_frame, commit
// b649dcb, tested in tests/test_dangling_local_struct_member_reuse.c).
//
// sum_vecs and mixed run first (same shapes as #727's own test,
// tests/test_attr_vector_size_variadic_dangling.c) purely to leave a dead
// sibling frame's own escaping `va_list ap` exact-recorded in
// stack_ptr_epochs. after8 has 8 fixed register-passed int params before
// '...', so its single variadic vector arg is stack-spilled (arg index >=
// 8) rather than register-passed; its own non-escaping vector local `v`
// (STKTAG'd per #727 but not exact-recorded, per #727's policy) can end up
// at the exact same physical address as the dead sibling's stale exact tag.
// Layer 2 (stack_ptr_epochs) used to short-circuit to "dangling" on that
// stale exact hit, without ever consulting layer 3's interval table (where
// after8's own live STKTAG range for `v` actually covers the address).
// `wide` repeats the shape with two back-to-back va_arg reads (256-bit and
// 512-bit vectors, #722).
//
// Fixed in op_CHKP3_fn (src/ops.c): a dead exact-tag hit now falls through
// to stack_interval_stab and only reports dangling if no *live* interval
// covers the address -- the same prefer-live arbitration layer 3 already
// had for #727, now also applied at layer 2.
#include <stdarg.h>

typedef float  v4sf __attribute__((vector_size(16)));
typedef float  v8sf __attribute__((vector_size(32)));
typedef double v8df __attribute__((vector_size(64)));

static int sum_vecs(int n, ...) {
    va_list ap;
    va_start(ap, n);
    float acc = 0;
    for (int i = 0; i < n; i++) {
        v4sf v  = va_arg(ap, v4sf);
        acc    += v[0] + v[1] + v[2] + v[3];
    }
    va_end(ap);
    return (int)acc;
}

static int mixed(int n, ...) {
    va_list ap;
    va_start(ap, n);
    v4sf v  = va_arg(ap, v4sf);
    int  i2 = va_arg(ap, int);
    va_end(ap);
    return (int)(v[0] + v[1] + v[2] + v[3]) + i2;
}

// 8 fixed register-passed args before '...': the variadic vector arg is
// stack-spilled (arg index >= 8) rather than register-passed -- #740's
// actual repro shape.
static int after8(int a, int b, int c, int d, int e, int f, int g, int h, ...) {
    va_list ap;
    va_start(ap, h);
    v4sf v = va_arg(ap, v4sf);
    va_end(ap);
    return (int)(v[0] + v[1] + v[2] + v[3]);
}

// Two back-to-back va_arg reads of wider vectors (256-bit, 512-bit, #722).
static int wide(int n, ...) {
    va_list ap;
    va_start(ap, n);
    v8sf a = va_arg(ap, v8sf);
    v8df b = va_arg(ap, v8df);
    va_end(ap);
    double acc = 0;
    for (int i = 0; i < 8; i++)
        acc += a[i];
    for (int i = 0; i < 8; i++)
        acc += b[i];
    return (int)acc;
}

int main(void) {
    v4sf a = {1, 2, 3, 4}, b = {10, 20, 30, 40};

    if (sum_vecs(2, a, b) != 110)
        return 1;
    if (mixed(0, a, 100) != 110)
        return 2;
    if (after8(1, 2, 3, 4, 5, 6, 7, 8, a) != 10)
        return 3;

    v8sf w1 = {1, 2, 3, 4, 5, 6, 7, 8};
    v8df w2 = {1, 2, 3, 4, 5, 6, 7, 8};
    if (wide(2, w1, w2) != 72)
        return 4;

    return 42;
}
