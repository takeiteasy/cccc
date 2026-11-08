// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: a compound-literal return= needs a struct or union return type

struct P {
    int x;
};

[[cccc::test(return = (struct P){.x = 1})]] int test_scalar_fn(void) {
    return 1;
}
