// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not to this declaration
// [[cccc::checked]] on a file-scope variable opens no region, so it is an error
// rather than silently ignored.

[[cccc::checked]] int x;

int main(void) {
    return 42;
}
