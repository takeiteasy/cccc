// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'struct Nope' is not declared

struct P {
    int x;
};

[[cccc::test(return = (struct Nope){.x = 1})]] struct P test_unknown_tag(void) {
    return (struct P){1};
}
