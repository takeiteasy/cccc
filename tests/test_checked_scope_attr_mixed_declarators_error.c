// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not to this declaration
// [[cccc::checked]] on a declaration that declares a variable and a function opens no region, so it is an error
// rather than silently ignored.

[[cccc::checked]] int x, f(void);

int main(void) {
    return 42;
}
