// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not to this declaration
// [[cccc::unchecked]] on a block-scope variable opens no region, so it is an error
// rather than silently ignored.

int main(void) {
    [[cccc::unchecked]] int y = 0;
    return 42 + y;
}
