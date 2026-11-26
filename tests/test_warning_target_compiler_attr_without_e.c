// CCCC_FLAGS: --output-compiler=gcc
// CCCC_EXPECT_STDERR: --output-compiler only affects -E output
__attribute__((overloadable)) int f(int x) {
    return x;
}
int main(void) {
    return f(42);
}
