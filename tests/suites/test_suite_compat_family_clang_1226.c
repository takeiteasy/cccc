// CCCC_FLAGS: --testing --compiler-family=clang
// The clang reading of the gcc/clang type-compatibility divergences, pinned
// directly (rather than only asserted by inspection). Under
// --compiler-family=clang, __builtin_types_compatible_p treats a top-level
// `_Atomic`, an array element's `_Atomic`, and a function type's own
// return-type cvr qualifiers as significant -- the numbers a stock clang
// install produces. Every value verified against clang (macOS aarch64,
// -std=c11). See man/TYPES.md.

typedef volatile int vf_ret_t(void);
typedef int plain_ret_t(void);
typedef _Atomic int ai_arr_t[3];
typedef int         i_arr_t[3];
typedef void cp_param_t(const int);
typedef void plain_param_t(int);

[[cccc::test(return = 42)]]
int test_compat_family_clang(void) {
    // top-level _Atomic is significant under clang
    if (__builtin_types_compatible_p(_Atomic int, int))
        return 1;
    // an array element's _Atomic is the array's top-level qualifier
    if (__builtin_types_compatible_p(ai_arr_t, i_arr_t))
        return 2;
    // a function type keeps its own return-type cvr under clang
    if (__builtin_types_compatible_p(vf_ret_t, plain_ret_t))
        return 3;

    // things clang and gcc still agree on stay agreed under the clang policy
    if (!__builtin_types_compatible_p(const int, int))
        return 4;
    if (!__builtin_types_compatible_p(int[3], int[3]))
        return 5;
    if (__builtin_types_compatible_p(_Atomic int *, int *))
        return 6;
    if (!__builtin_types_compatible_p(cp_param_t, plain_param_t))
        return 7;

    // _Generic arm selection is not policy-sensitive
    _Atomic int ai = 0;
    if (_Generic(ai, int: 42, default: 0) != 42)
        return 8;

    return 42;
}
