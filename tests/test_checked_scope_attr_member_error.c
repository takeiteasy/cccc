// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not to this declaration
// [[cccc::checked]] on a struct member opens no region, so it is an error
// rather than silently ignored.

struct S {
    [[cccc::checked]] int m;
};

int main(void) {
    return 42;
}
