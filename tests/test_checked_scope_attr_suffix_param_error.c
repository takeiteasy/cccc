// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not after a declarator
// [[cccc::checked]] after a parameter declarator opens no region, so it is an error
// rather than silently ignored.

void f(int a [[cccc::checked]]) {
    (void)a;
}

int main(void) {
    return 42;
}
