// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'union P' names a struct

struct P {
    int x;
};

[[cccc::test(return = (union P){.x = 1})]] struct P test_wrong_kind(void) {
    return (struct P){1};
}
