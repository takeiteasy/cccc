// Tests for [[cccc::test(flags = "--optimize=N ...")]] — per-test optimisation
// and combined flags via the flags= attribute (ticket #356).
// CCCC_FLAGS: --testing

// Multiple whitespace-separated flags in one flags= string.
[[cccc::test(flags = "--optimize=2 --safety=1", return = 2)]]
int test_combined_flags(void) {
    return 1 + 1;
}

// Long form: --optimize=N
[[cccc::test(flags = "--optimize=3", return = 42)]]
int test_opt3(void) {
    int x = 6;
    int y = 7;
    return x * y;
}

// Short -O form.
[[cccc::test(flags = "-O2", return = 5)]]
int test_opt_short(void) {
    return 2 + 3;
}

// Safety preset: --safety=standard enables standard safety bits.
[[cccc::test(flags = "--safety=standard", return = 3)]]
int test_safety_standard(void) {
    int a = 1, b = 2;
    return a + b;
}

// Safety preset: -2 (shorthand for standard).
[[cccc::test(flags = "-2", return = 100)]]
int test_safety_short(void) {
    return 100;
}

// Unflagged test after all the flagged ones — exercises lazy restore.
[[cccc::test(return = 0)]]
int test_baseline_after_optimised(void) {
    return 0;
}
