// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: missing value in return= compound literal

struct P {
    int x;
};

[[cccc::test(return = (struct P){.x = })]] struct P
    test_missing_field_value(void) {
    return (struct P){1};
}
