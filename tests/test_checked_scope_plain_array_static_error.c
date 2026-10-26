// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array local variable
// A static plain array local is not allowed in a checked region.

[[cccc::checked]]
int f(void) {
    static int a[3] = {1, 2, 3};
    return a[0];
}

int main(void) {
    return 42;
}
