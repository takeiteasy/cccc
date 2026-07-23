// Vector-by-value arguments through a variadic '...' parameter (tracker
// #721, follow-up to #714/#722). #714 rejected this case: variadic args
// only had an int/float register-spill classification, with no by-memory
// path. #721 lifts the restriction -- a variadic vector arg works exactly
// like a fixed-position one (gen_vector_arg_ptr, #714): the caller copies
// the value into a frame scratch slot and passes the slot's *address*, which
// always occupies exactly one 8-byte arg/stack slot no matter how wide the
// vector is (16/32/64 bytes, #722). <stdarg.h>'s va_arg detects a vector
// `type` via __builtin_classify_type (new in #721) and dereferences the
// slot instead of reading it directly. Verified against gcc/clang: both
// accept a vector_size vector through '...' and read it back correctly, so
// this mirrors their behavior (CCCC's own ABI on both ends, not the
// platform ABI -- see COVERAGE.md).
//
// See tests/test_attr_vector_size_variadic_ffi_error.c for the still-
// rejected case (vector through variadic FFI/libffi calls, out of scope;
// tracked separately as #726).
//
// Not exercised under -3 here: two variadic functions that each read a
// vector via va_arg, called in a specific order, can trip a false positive
// in the -3 dangling-pointer detector's interior-pointer interval-stab
// (confirmed a false positive, not an actual dangling read -- the value
// read is correct and does not depend on call order; only the detector's
// bookkeeping does). That is a narrow, pre-existing gap in the detector's
// STKTAG/epoch-recency design (#669/#670/#673/#675), newly exposed by this
// access pattern rather than caused by it, and is tracked separately as
// #727. This test's scenarios are otherwise verified correct at -0/-1/-2,
// -O3, and under the --c4 bytecode round-trip.

#include <stdarg.h>

typedef float v4sf __attribute__((vector_size(16)));
typedef float v8sf __attribute__((vector_size(32)));
typedef double v8df __attribute__((vector_size(64)));

// Single vector arg through '...', register-passed (well under 8 args).
static int sum_vecs(int n, ...) {
    va_list ap;
    va_start(ap, n);
    float acc = 0;
    for (int i = 0; i < n; i++) {
        v4sf v = va_arg(ap, v4sf);
        acc += v[0] + v[1] + v[2] + v[3];
    }
    va_end(ap);
    return (int)acc;
}

// Interleaved scalar + vector variadic args.
static int mixed(int n, ...) {
    va_list ap;
    va_start(ap, n);
    v4sf v = va_arg(ap, v4sf);
    int i2 = va_arg(ap, int);
    va_end(ap);
    return (int)(v[0] + v[1] + v[2] + v[3]) + i2;
}

// 8 fixed register-passed args before '...': the variadic vector arg is
// stack-spilled (arg index >= 8) rather than register-passed.
static int after8(int a, int b, int c, int d, int e, int f, int g, int h,
                   ...) {
    va_list ap;
    va_start(ap, h);
    v4sf v = va_arg(ap, v4sf);
    va_end(ap);
    return (int)(v[0] + v[1] + v[2] + v[3]);
}

// Wider vectors (256-bit, 512-bit, #722) through '...'.
static int wide(int n, ...) {
    va_list ap;
    va_start(ap, n);
    v8sf a = va_arg(ap, v8sf);
    v8df b = va_arg(ap, v8df);
    va_end(ap);
    double acc = 0;
    for (int i = 0; i < 8; i++) acc += a[i];
    for (int i = 0; i < 8; i++) acc += b[i];
    return (int)acc;
}

int main(void) {
    v4sf a = {1, 2, 3, 4}, b = {10, 20, 30, 40};

    if (sum_vecs(2, a, b) != 110) return 1;

    if (mixed(0, a, 100) != 110) return 2;

    if (after8(1, 2, 3, 4, 5, 6, 7, 8, a) != 10) return 3;

    v8sf w1 = {1, 2, 3, 4, 5, 6, 7, 8};
    v8df w2 = {1, 2, 3, 4, 5, 6, 7, 8};
    if (wide(2, w1, w2) != 72) return 4;

    return 42;
}
