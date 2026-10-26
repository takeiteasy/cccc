// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not after a declarator
// [[cccc::checked]] after the parameter list of a prototype opens no region, so it is an error
// rather than silently ignored.

void f(void) [[cccc::checked]];

int main(void) {
    return 42;
}
