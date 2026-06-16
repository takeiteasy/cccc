// Tests lazy recompile with mixed flagged/unflagged tests and shared flags
// in the same file (ticket #356).
// CCCC_FLAGS: --testing

// Two tests sharing the same flags= string — should trigger one recompile,
// not two.
[[cccc::test(flags = "--bounds-checks --overflow-checks", return = 7)]]
int test_shared_flags_first(void) {
    return 3 + 4;
}

[[cccc::test(flags = "--bounds-checks --overflow-checks", return = 12)]]
int test_shared_flags_second(void) {
    return 12;
}

// Back to base (unflagged) — exercises restore path.
[[cccc::test(return = 1)]]
int test_unflagged_between(void) {
    return 1;
}

// Different flags — exercises second recompile.
[[cccc::test(flags = "--optimize=1", return = 9)]]
int test_different_flags(void) {
    return 9;
}

// Unflagged at the end — final restore.
[[cccc::test(return = 55)]]
int test_unflagged_at_end(void) {
    return 55;
}
