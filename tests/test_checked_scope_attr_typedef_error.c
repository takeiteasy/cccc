// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not to this declaration
// [[cccc::checked]] on a typedef opens no region, so it is an error
// rather than silently ignored.

[[cccc::checked]] typedef int T;

int main(void) {
    return 42;
}
