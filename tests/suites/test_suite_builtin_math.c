// libm __builtin_ aliases need no <math.h>.

#if !__has_builtin(__builtin_fabs) || !__has_builtin(__builtin_fmaf) ||        \
    !__has_builtin(__builtin_copysignl)
#error "libm builtins not reported by __has_builtin"
#endif
#if __has_builtin(__builtin_fabsx) || __has_builtin(__builtin_fmaxx)
#error "bogus libm builtin reported by __has_builtin"
#endif

[[cccc::test(return = 42)]]
int test_unary(void) {
    double d = __builtin_fabs(-42.0);
    float  f = __builtin_sqrtf(1764.0f);
    if (d != 42.0 || f != 42.0f)
        return 1;
    if (__builtin_floor(42.9) != 42.0 || __builtin_ceil(41.1) != 42.0)
        return 2;
    if (__builtin_trunc(42.9) != 42.0 || __builtin_round(41.6) != 42.0)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_binary_ternary(void) {
    if (__builtin_copysign(42.0, -1.0) != -42.0)
        return 1;
    if (__builtin_fmin(50.0, 42.0) != 42.0 || __builtin_fmax(7.0, 42.0) != 42.0)
        return 2;
    if (__builtin_fma(6.0, 7.0, 0.0) != 42.0)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_variants(void) {
    if (__builtin_fabsf(-42.0f) != 42.0f)
        return 1;
    if (__builtin_fabsl(-42.0L) != 42.0L)
        return 2;
    if (__builtin_fmaxf(1.0f, 42.0f) != 42.0f)
        return 3;
    if (__builtin_fmal(6.0L, 7.0L, 0.0L) != 42.0L)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_argument_conversion(void) {
    // Integer and mixed-width arguments convert to the parameter type.
    if (__builtin_fabs(-42) != 42.0)
        return 1;
    if (__builtin_sqrtf(1764.0) != 42.0f)
        return 2;
    return 42;
}

// Constant arguments fold, so the calls are valid in constant contexts.
static double g_fabs  = __builtin_fabs(-42.0);
static float  g_sqrtf = __builtin_sqrtf(1764.0f);
static double g_floor = __builtin_floor(42.9);
static double g_ceil  = __builtin_ceil(41.1);
static double g_trunc = __builtin_trunc(-42.9);
static double g_round = __builtin_round(41.6);
static double g_copy  = __builtin_copysign(42.0, -1.0);
static double g_fmin  = __builtin_fmin(50.0, 42.0);
static double g_fmax  = __builtin_fmax(42.0, __builtin_nan(""));
static double g_fma   = __builtin_fma(6.0, 7.0, 0.0);
static long double g_fmal  = __builtin_fmal(6.0L, 7.0L, 0.0L);
static double      g_inf   = __builtin_fabs(-__builtin_inf());
static float       g_fmaf  = __builtin_fmaf(16777216.0f, 1.0f, 1.0f);
static double      g_sqrt2 = __builtin_sqrt(2.0);

_Static_assert(__builtin_fabs(-1.0) == 1.0, "fabs folds");
constexpr double k_floor = __builtin_floor(2.5);
_Static_assert(k_floor == 2.0, "constexpr floor");

[[cccc::test(return = 42)]]
int test_static_initializers(void) {
    if (g_fabs != 42.0 || g_sqrtf != 42.0f || g_floor != 42.0)
        return 1;
    if (g_ceil != 42.0 || g_trunc != -42.0 || g_round != 42.0)
        return 2;
    if (g_fmin != 42.0 || g_fmax != 42.0 || g_fma != 42.0 || g_fmal != 42.0L)
        return 3;
    if (g_copy != -42.0)
        return 4;
    if (g_inf != __builtin_inf())
        return 5;
    // Single rounding in float: 2^24 + 1 is not representable.
    if (g_fmaf != 16777216.0f)
        return 6;
    if (g_sqrt2 * g_sqrt2 < 1.99 || g_sqrt2 * g_sqrt2 > 2.01)
        return 7;
    return 42;
}

[[cccc::test(return = 42)]]
int test_constant_array_size(void) {
    int a[(int)__builtin_floor(42.9)];
    return sizeof a / sizeof a[0];
}

[[cccc::test(return = 42)]]
int test_fold_keeps_side_effects(void) {
    int n = 0;
    double d = __builtin_fabs((++n, -42.0));
    return n == 1 && d == 42.0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_domain_errors_stay_runtime(void) {
    volatile double neg = -1.0;
    double          s   = __builtin_sqrt(-1.0);
    double          f   = __builtin_fma(1e308, 10.0, 0.0);
    if (s == s || neg != -1.0)
        return 1;
    if (f != __builtin_inf())
        return 2;
    return 42;
}

#if !__has_builtin(__builtin_ldexp) || !__has_builtin(__builtin_ilogbf) ||     \
    !__has_builtin(__builtin_malloc) || !__has_builtin(__builtin_free)
#error "transcendental, ldexp/ilogb or malloc/free builtins not reported"
#endif

[[cccc::test(return = 42)]]
int test_transcendental(void) {
    volatile double x = 0.5, y = 3.0;
    if (__builtin_fabs(__builtin_asin(x) - 0.5235987755982989) > 1e-12)
        return 1;
    if (__builtin_fabs(__builtin_acos(x) - 1.0471975511965979) > 1e-12)
        return 2;
    if (__builtin_fabs(__builtin_atan(x) - 0.4636476090008061) > 1e-12)
        return 3;
    if (__builtin_fabs(__builtin_atan2(x, y) - 0.16514867741462683) > 1e-12)
        return 4;
    if (__builtin_fabs(__builtin_sin(x) - 0.479425538604203) > 1e-12 ||
        __builtin_fabs(__builtin_cos(x) - 0.8775825618903728) > 1e-12 ||
        __builtin_fabs(__builtin_tan(x) - 0.5463024898437905) > 1e-12)
        return 5;
    if (__builtin_fmod(y, x) != 0.0 || __builtin_pow(y, 2.0) != 9.0)
        return 6;
    if (__builtin_sinf(0.0f) != 0.0f || __builtin_powl(2.0L, 3.0L) != 8.0L)
        return 7;
    return 42;
}

[[cccc::test(return = 42)]]
int test_rint_ldexp_ilogb(void) {
    volatile double x = 0.5;
    volatile int    n = 4;
    if (__builtin_rint(2.5) != 2.0 || __builtin_rintf(3.5f) != 4.0f)
        return 1;
    if (__builtin_ldexp(x, n) != 8.0 || __builtin_ldexp(1.0, 3) != 8.0 ||
        __builtin_ldexpf(1.0f, 2) != 4.0f || __builtin_ldexpl(1.0L, 5) != 32.0L)
        return 2;
    if (__builtin_ilogb(3.0) != 1 || __builtin_ilogbf(8.0f) != 3 ||
        __builtin_ilogbl(0.001L) != -10)
        return 3;
    int shift = __builtin_ilogb(1024.0);
    return shift == 10 ? 42 : 4;
}

[[cccc::test(return = 42)]]
int test_fold_stays_runtime_for_errors(void) {
    double inf = __builtin_pow(10.0, 400.0);
    double nan = __builtin_asin(2.0);
    double big = __builtin_ldexp(1.0, 2000);
    if (inf != __builtin_inf() || nan == nan || big != __builtin_inf())
        return 1;
    return 42;
}

[[cccc::test(return = 42)]]
int test_builtin_malloc_free(void) {
    int *p = __builtin_malloc(sizeof(int) * 4);
    if (!p)
        return 1;
    p[3]  = 42;
    int v = p[3];
    __builtin_free(p);
    return v;
}
