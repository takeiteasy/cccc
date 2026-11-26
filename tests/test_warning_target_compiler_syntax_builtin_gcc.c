// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDERR: error: '__builtin_assume' is not supported by gcc \[-Wtarget-compiler\]
void f(int x) {
    __builtin_assume(x > 0);
}
int main(void) {
    return 42;
}
