// Fixture TU0 for tests/test_testing_multi_tu_1007.c (#1007). Deliberately
// has no [[cccc::test]] functions and no #include of testing.h -- the bug
// this test guards against is that the --testing mode header (its Assert*
// macros and __builtin_assert_* prototypes) failed to reach a *later* input
// file's parse stream once each command-line input file became its own
// translation unit (#1001).
int testing_multi_tu_1007_helper(void) { return 42; }
