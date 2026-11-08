// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: expected '=' after the field name 'b'

struct P {
    int a;
    int b;
};

[[cccc::test(return = (struct P){.a = 1, .b})]] struct P
    test_missing_equals(void) {
    return (struct P){1, 2};
}
