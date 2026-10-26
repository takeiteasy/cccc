// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not after a declarator
// [[cccc::checked]] after the parameter list of a definition opens no region, so it is an error
// rather than silently ignored.

void f(void) [[cccc::checked]] {
    int *p = 0;
    (void)p;
}

int main(void) {
    return 42;
}
