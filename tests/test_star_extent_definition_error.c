// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: star modifier used outside of function prototype
// `[*]` is only valid in a prototype that is not a definition.

int f(int n, int a[n][*]) {
    return 0;
}

int main(void) {
    return 42;
}
