// Tests for [[cccc::test(flags = "--bounds-checks")]] — per-test bounds
// checking via the flags= attribute (ticket #356).
// CCCC_FLAGS: --testing

// Verify that flags= "--bounds-checks" enables CCCC_BOUNDS_CHECKS for this
// test's compilation.  The test itself only exercises in-bounds access (so it
// passes with or without bounds checks), but the attribute is parsed and
// applied via a per-test recompile.
[[cccc::test(flags = "--bounds-checks", return = 6)]]
int test_bounds_in_bounds(void) {
    int arr[4] = {1, 2, 3, 6};
    return arr[3];
}

// Verify a short-hand flag form (-b) also parses.
[[cccc::test(flags = "-b", return = 10)]]
int test_bounds_short_flag(void) {
    int arr[3] = {10, 20, 30};
    return arr[0];
}

// Unflagged test — runs under the base compile, after the above tests have
// triggered a per-test recompile.  Exercises the restore-to-base path.
[[cccc::test(return = 99)]]
int test_bounds_after_flagged(void) {
    return 99;
}
