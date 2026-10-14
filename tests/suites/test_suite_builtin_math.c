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
