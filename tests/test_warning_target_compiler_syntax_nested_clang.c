// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=clang
// CCCC_EXPECT_STDERR: error: nested functions are not supported by clang \[-Wtarget-compiler\]
int f(int x) {
    int g(int y) {
        return x + y;
    }
    return g(1);
}
int main(void) {
    return 42;
}
