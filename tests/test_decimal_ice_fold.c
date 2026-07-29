// CCCC_FLAGS: --testing
// C23 (#832): the positive counterpart to test_decimal_ice_error.c. A
// floating constant is legal as the *immediate* operand of a cast used in
// an integer constant expression (ISO C's rule, not GCC-specific) --
// `(int)1.5dd` in an array bound now folds via eval2's ND_CAST arm calling
// eval_decimal + cccc_dec_to_int, where before #832 it was rejected
// outright (see test_decimal_ice_error.c's old shape, now changed to a bare
// uncast literal since this case stopped being an error). Guarded like
// every other decimal test: a no-op pass in the default (decimal-off)
// build, since a decimal literal itself requires CCCC_HAS_DECIMAL=1.

#ifdef __STDC_IEC_60559_DFP__
int arr[(int)1.5dd]; // == int arr[1];
#endif

[[cccc::test(return = 42)]]
int test_decimal_ice_fold(void) {
#ifdef __STDC_IEC_60559_DFP__
    if (sizeof(arr) / sizeof(arr[0]) != 1)
        return 1;
#endif
    return 42;
}
