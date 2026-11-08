// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: return= operand is not an integer, floating or null pointer constant expression

struct P {
    int x;
};

int answer(void);

[[cccc::test(return = (struct P){.x = answer()})]] struct P
    test_field_is_not_constant(void) {
    return (struct P){42};
}
