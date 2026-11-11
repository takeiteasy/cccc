// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: 'test' is a cccc attribute; write \[\[cccc::test\]\]

[[gnu::test]] void not_a_test(void) {}

int main(void) {
    return 0;
}
