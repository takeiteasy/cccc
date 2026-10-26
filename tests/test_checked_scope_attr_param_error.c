// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not to this declaration
// [[cccc::checked]] on a parameter opens no region, so it is an error
// rather than silently ignored.

void f([[cccc::checked]] int a) {
    (void)a;
}

int main(void) {
    return 42;
}
