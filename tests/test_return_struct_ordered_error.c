// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: a struct return= assertion supports only '=' and '!='

struct P {
    int x;
};

[[cccc::test(return < (struct P){.x = 1})]] struct P
    test_ordered_struct_comparison(void) {
    return (struct P){1};
}
