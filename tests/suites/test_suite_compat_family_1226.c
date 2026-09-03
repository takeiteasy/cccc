// CCCC_FLAGS: --testing
// Type-compatibility divergences between the gcc and clang compiler families,
// and the --compiler-family policy switch that selects which one CCCC's own
// front end models (__builtin_types_compatible_p, _Generic arm selection).
//
// The default policy is gcc -- today's behaviour, unchanged. This file runs
// under the default, so the policy-sensitive rows below branch on
// __CCCC_COMPILER_FAMILY__ (0 = gcc, 1 = clang) and would also pass verbatim
// under --compiler-family=clang. The unconditional rows are cases both
// families agree on; test_suite_compat_family_clang_1226.c pins the clang
// readings directly.
//
// Every value here was measured against gcc-16 and clang (macOS aarch64,
// -std=c11). See man/TYPES.md.

typedef _Atomic int ai_t;
typedef volatile int vf_ret_t(void);
typedef int plain_ret_t(void);
typedef _Atomic int af_ret_t(void);
typedef void ap_param_t(_Atomic int);
typedef void plain_param_t(int);
typedef void cp_param_t(const int);
typedef _Atomic int ai_arr_t[3];
typedef int         i_arr_t[3];

[[cccc::test(return = 42)]]
int test_compat_family_agreed(void) {
    // arrays: both families agree
    if (!__builtin_types_compatible_p(int[3], int[3]))
        return 1;
    if (__builtin_types_compatible_p(int[3], int[4]))
        return 2;
    if (!__builtin_types_compatible_p(int[3], int[]))
        return 3;

    // _Atomic below the top level: significant under both families
    if (__builtin_types_compatible_p(_Atomic int *, int *))
        return 4;
    if (__builtin_types_compatible_p(ap_param_t, plain_param_t))
        return 5;

    // cvr below the top level and at a parameter: unchanged
    if (__builtin_types_compatible_p(const int *, int *))
        return 6;
    if (!__builtin_types_compatible_p(cp_param_t, plain_param_t))
        return 7;

    // top-level cvr: dropped by both
    if (!__builtin_types_compatible_p(const int, int))
        return 8;

    // _Generic: an `_Atomic T:` arm and a plain `T:` arm coexist, and an
    // unqualified control picks the plain one -- both families
    _Atomic int ai = 0;
    if (_Generic(ai, ai_t: 1, int: 2, default: 0) != 2)
        return 9;
    if (_Generic((int)0, ai_t: 1, int: 2, default: 0) != 2)
        return 10;

    return 42;
}

[[cccc::test(return = 42)]]
int test_compat_family_policy(void) {
    // top-level _Atomic: gcc 1, clang 0
    int atomic_top = __builtin_types_compatible_p(_Atomic int, int);
    // a function type's own return-type cvr: gcc 1, clang 0
    int ret_cvr = __builtin_types_compatible_p(vf_ret_t, plain_ret_t);
    // a function's return-type _Atomic: 0 under both (gcc drops cvr there
    // but not _Atomic)
    int ret_atomic = __builtin_types_compatible_p(af_ret_t, plain_ret_t);
    // an array element's _Atomic: gcc 1, clang 0 (top-level of the array)
    int arr_atomic = __builtin_types_compatible_p(ai_arr_t, i_arr_t);

#if __CCCC_COMPILER_FAMILY__ == 1 // clang
    if (atomic_top != 0)
        return 1;
    if (ret_cvr != 0)
        return 2;
    if (arr_atomic != 0)
        return 4;
#else // gcc (default)
    if (atomic_top != 1)
        return 1;
    if (ret_cvr != 1)
        return 2;
    if (arr_atomic != 1)
        return 4;
#endif
    if (ret_atomic != 0)
        return 3;

    return 42;
}
