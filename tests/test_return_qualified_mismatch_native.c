// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing=native
// CCCC_EXPECT_STDERR: 'struct Q' does not match the function's return type

struct P {
    int x;
};
struct Q {
    int x;
};

[[cccc::test(return = (const struct Q){.x = 1})]] struct P test_qualified_wrong_tag(void) {
    return (struct P){1};
}
