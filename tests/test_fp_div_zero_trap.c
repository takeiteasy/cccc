// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --trap-fp-divzero
// #773: --trap-fp-divzero opts back into the pre-#773 behavior of aborting
// on any float division by zero, for debugging code that assumes it is
// always a bug. Without the flag, this same program returns 42 (see
// test_fp_div_zero_ieee.c) since plain IEEE division is now the default.

int main(void) {
    volatile double zero = 0.0;
    double          r    = 1.0 / zero;
    (void)r;
    return 42;
}
