// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not to this declaration
// [[cccc::checked]] on a for-init declaration opens no region, so it is an error
// rather than silently ignored.

int main(void) {
    for ([[cccc::checked]] int i = 0; i < 1; i++) {
    }
    return 42;
}
