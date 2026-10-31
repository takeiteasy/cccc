// CCCC_FLAGS: --testing

// A floating-point or complex value used as a truth value is true when it is
// nonzero (NaN included), in every context: conditions, !, &&, ||, ?:, and
// conversions to _Bool, at run time and in constant expressions.

#include <complex.h>

#define FP_TRUTH(T, N)                                                         \
    static int N##_if(T d) {                                                   \
        int g = 0;                                                             \
        if (d)                                                                 \
            g = 1;                                                             \
        return g;                                                              \
    }                                                                          \
    static int N##_while(T d) {                                                \
        int n = 0;                                                             \
        while (d) {                                                            \
            if (++n >= 3)                                                      \
                break;                                                         \
            d = 0;                                                             \
        }                                                                      \
        return n;                                                              \
    }                                                                          \
    static int N##_for(T d) {                                                  \
        int n = 0;                                                             \
        for (; d; d = 0)                                                       \
            if (++n >= 3)                                                      \
                break;                                                         \
        return n;                                                              \
    }                                                                          \
    static int N##_do(T d) {                                                   \
        int n = 0;                                                             \
        do {                                                                   \
            n++;                                                               \
        } while (d && n < 2);                                                  \
        return n == 2;                                                         \
    }                                                                          \
    static int N##_cond(T d) {                                                 \
        return d ? 1 : 0;                                                      \
    }                                                                          \
    static int N##_and(T d, int one) {                                         \
        return d && one;                                                       \
    }                                                                          \
    static int N##_or(T d, int zero) {                                         \
        return d || zero;                                                      \
    }                                                                          \
    static int N##_not(T d) {                                                  \
        return !d;                                                             \
    }                                                                          \
    static int N##_notnot(T d) {                                               \
        return !!d;                                                            \
    }                                                                          \
    static int N##_andnot(T d, T e) {                                          \
        return !d && !e;                                                       \
    }                                                                          \
    static _Bool N##_bool_ret(T d) {                                           \
        return d;                                                              \
    }                                                                          \
    static int N##_bool_cast(T d) {                                            \
        return (_Bool)d;                                                       \
    }                                                                          \
    static int N##_bool_assign(T d) {                                          \
        _Bool b = d;                                                           \
        return b;                                                              \
    }                                                                          \
    static int N##_bool_arg(_Bool b) {                                         \
        return b;                                                              \
    }                                                                          \
    static void N##_check(T d, int want) {                                     \
        AssertEq(N##_if(d), want);                                             \
        AssertEq(N##_while(d), want);                                          \
        AssertEq(N##_for(d), want);                                            \
        AssertEq(N##_do(d), want);                                             \
        AssertEq(N##_cond(d), want);                                           \
        AssertEq(N##_and(d, 1), want);                                         \
        AssertEq(N##_and(d, 0), 0);                                            \
        AssertEq(N##_or(d, 0), want);                                          \
        AssertEq(N##_or(d, 1), 1);                                             \
        AssertEq(N##_not(d), !want);                                           \
        AssertEq(N##_notnot(d), want);                                         \
        AssertEq(N##_andnot(d, d), !want);                                     \
        AssertEq((int)N##_bool_ret(d), want);                                  \
        AssertEq(N##_bool_cast(d), want);                                      \
        AssertEq(N##_bool_assign(d), want);                                    \
        AssertEq(N##_bool_arg(d), want);                                       \
    }                                                                          \
    static void N##_all(void) {                                                \
        N##_check(0.0, 0);                                                     \
        N##_check(-0.0, 0);                                                    \
        N##_check(0.5, 1);                                                     \
        N##_check(-3.0, 1);                                                    \
        N##_check(1e-30, 1);                                                   \
        N##_check(__builtin_nan(""), 1);                                       \
        N##_check(__builtin_inf(), 1);                                         \
        N##_check(-__builtin_inf(), 1);                                        \
    }

FP_TRUTH(double, dbl)
FP_TRUTH(float, flt)
FP_TRUTH(long double, ldbl)

[[cccc::test]]
void test_double_truth(void) {
    dbl_all();
}

[[cccc::test]]
void test_float_truth(void) {
    flt_all();
}

[[cccc::test]]
void test_long_double_truth(void) {
    ldbl_all();
}

// A float narrower than its double value: 1e-300 is nonzero as a double but
// rounds to zero as a float.
[[cccc::test]]
void test_float_underflow_is_false(void) {
    double d = 1e-300;
    AssertEq(dbl_if(d), 1);
    AssertEq(flt_if((float)d), 0);
}

#define CPLX_CHECK(ZEXPR, WANT)                                                \
    do {                                                                       \
        double _Complex z = (ZEXPR);                                           \
        int   a           = z ? 1 : 0;                                         \
        int   b           = !z;                                                \
        int   c           = !!z;                                               \
        _Bool d           = z;                                                 \
        int   e           = 0;                                                 \
        if (z)                                                                 \
            e = 1;                                                             \
        int f = 0;                                                             \
        while (z) {                                                            \
            if (++f >= 3)                                                      \
                break;                                                         \
            z = 0;                                                             \
        }                                                                      \
        int g = z || 0;                                                        \
        AssertEq(a, (WANT));                                                   \
        AssertEq(b, !(WANT));                                                  \
        AssertEq(c, (WANT));                                                   \
        AssertEq((int)d, (WANT));                                              \
        AssertEq(e, (WANT));                                                   \
        AssertEq(f, (WANT));                                                   \
        AssertEq(g, 0);                                                        \
    } while (0)

[[cccc::test]]
void test_complex_truth(void) {
    CPLX_CHECK(CMPLX(0.0, 0.0), 0);
    CPLX_CHECK(CMPLX(-0.0, -0.0), 0);
    CPLX_CHECK(CMPLX(0.5, 0.0), 1);
    CPLX_CHECK(CMPLX(0.0, 0.5), 1);
    CPLX_CHECK(CMPLX(-2.0, 3.0), 1);
    CPLX_CHECK(CMPLX(__builtin_nan(""), 0.0), 1);
}

[[cccc::test]]
void test_complex_float_truth(void) {
    float _Complex z = CMPLXF(0.0f, 0.0f);
    AssertEq(z ? 1 : 0, 0);
    AssertEq(!z, 1);
    z = CMPLXF(0.0f, 0.25f);
    AssertEq(z ? 1 : 0, 1);
    AssertEq(!z, 0);
    AssertEq((int)(_Bool)z, 1);
}

// Evaluated while parsing: the same answers as at run time.
_Bool        g_half     = 0.5;
_Bool        g_zero     = 0.0;
_Bool        g_negzero  = -0.0;
_Bool        g_imag     = CMPLX(0.0, 0.5);
_Bool        g_czero    = CMPLX(0.0, 0.0);
static _Bool g_tiny     = 1e-300;
int          g_cond     = 0.5 ? 1 : 2;
int          g_cond0    = 0.0 ? 1 : 2;
int          g_not      = !0.5;
int          g_not0     = !0.0;
int          g_and      = 0.5 && 0.25;
int          g_and0     = 0.5 && 0.0;
int          g_or       = 0.0 || 0.5;
int          g_cplx_not = !CMPLX(0.0, 0.5);

[[cccc::test]]
void test_constant_initializers(void) {
    AssertEq((int)g_half, 1);
    AssertEq((int)g_zero, 0);
    AssertEq((int)g_negzero, 0);
    AssertEq((int)g_imag, 1);
    AssertEq((int)g_czero, 0);
    AssertEq((int)g_tiny, 1);
    AssertEq(g_cond, 1);
    AssertEq(g_cond0, 2);
    AssertEq(g_not, 0);
    AssertEq(g_not0, 1);
    AssertEq(g_and, 1);
    AssertEq(g_and0, 0);
    AssertEq(g_or, 1);
    AssertEq(g_cplx_not, 0);
}

[[cccc::test]]
void test_static_local_initializer(void) {
    static _Bool s_half = 0.25;
    static _Bool s_zero = 0.0;
    AssertEq((int)s_half, 1);
    AssertEq((int)s_zero, 0);
}

[[cccc::test]]
void test_constant_conditions(void) {
    int r = 0;
    if (0.5)
        r |= 1;
    if (!0.0)
        r |= 2;
    while (0.5) {
        r |= 4;
        break;
    }
    if (0.0)
        r |= 8;
    if (!0.5)
        r |= 16;
    for (; 0.25;) {
        r |= 32;
        break;
    }
    AssertEq(r, 1 | 2 | 4 | 32);
}

struct Flags {
    _Bool         b   : 1;
    unsigned char pad : 3;
};

[[cccc::test]]
void test_bool_stores(void) {
    double d  = 0.5;
    _Bool  b  = 0;
    b        += d;
    AssertEq((int)b, 1);

    struct Flags s = {0};
    s.b            = d;
    AssertEq((int)s.b, 1);
    d   = 0.0;
    s.b = d;
    AssertEq((int)s.b, 0);

    _Bool arr[2] = {0.5, 0.0};
    AssertEq((int)arr[0], 1);
    AssertEq((int)arr[1], 0);
}

[[cccc::test]]
void test_short_circuit_still_skips(void) {
    int    calls = 0;
    double d     = 0.0;
    if (d && ++calls)
        calls += 10;
    AssertEq(calls, 0);
    d = 0.5;
    if (d || ++calls)
        calls += 100;
    AssertEq(calls, 100);
}
