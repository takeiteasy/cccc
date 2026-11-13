// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing -c=native -o /dev/null
// CCCC_EXPECT_STDERR: use --testing=native
// CCCC_REJECT_STDERR: Undefined symbols|ld returned
// A suite-only file has no main(): the prepass refuses before running it.
[[cccc::test(return = 42)]]
int test_no_main(void) {
    return 42;
}
