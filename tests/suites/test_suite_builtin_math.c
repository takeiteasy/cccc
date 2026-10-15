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
