// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: no \[\[cccc::test\]\] functions found
//
// #1007 (secondary issue): --testing exited 0 when zero [[cccc::test]]
// functions were collected -- a bare "TAP version 13 / 1..0" reads as a
// clean pass, so a mis-ordered multi-file invocation whose test file's
// declarations never reached the parser looked identical to a genuinely
// empty (but intentional) test run. Fixed by cc_run_tests (testing.c)
// hard-erroring whenever vm->compiler.test_fns is NULL, before the TAP/
// plain/JSON header is printed. A --test=GLOB/--test-suite filter that
// matches nothing is unaffected -- this checks the pre-filter list, not
// the post-filter count.
int test_testing_no_tests_1007_unused(void) {
    return 0;
}
