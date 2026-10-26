// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not after a declarator
// [[cccc::checked]] after a typedef declarator opens no region, so it is an error
// rather than silently ignored.

typedef int T [[cccc::checked]];

int main(void) {
    return 42;
}
