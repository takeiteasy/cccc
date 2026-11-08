// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: only a null pointer can be asserted for a pointer

int *address(void);

[[cccc::test(return = 5)]]
int *test_pointer_return_address(void) {
    return address();
}
