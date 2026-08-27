// CCCC_FLAGS: tests/fixtures/testing_multi_tu_1007_helper.c --testing
//
// #1007: `cccc -t two.c helper.c` (test file first) passed, but
// `cccc -t helper.c two.c` (test file second) failed with
// "undefined function: AssertEq". Root cause: cc_inject_test_header()
// (testing.c) was called once, before the preprocess loop, so its side
// effect -- registering the Assert* macros into vm.compiler.macros -- was
// undone for every TU after the first by
// cc_reset_preprocessor_state_for_next_tu() (#1001's own per-TU isolation
// fix, preprocess.c), and its returned __builtin_assert_* prototypes were
// spliced onto input_tokens[0] only. Fixed by re-injecting the mode header
// inside the per-TU preprocess loop in main.c, after the per-TU reset, so
// both the macro registration and the prototype splice reach every TU.
//
// This file is command-line input 1 (the helper fixture above is input 0),
// exercising exactly the previously-broken order.
int testing_multi_tu_1007_helper(void);

[[cccc::test]]
void test_multi_tu_second_file(void) {
    AssertEq(testing_multi_tu_1007_helper(), 42);
}
