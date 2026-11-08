// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: return= operand is not an integer or floating constant expression

int answer(void);

[[cccc::test(return = answer())]]
int test_call_is_not_constant(void) {
    return 42;
}
