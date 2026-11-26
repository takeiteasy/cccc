// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_REJECT_STDERR: target-compiler|error
// CCCC_EXPECT_STDOUT: int g\(int y\)
int f(int x) {
    int g(int y) {
        return x + y;
    }
    return g(1);
}
int main(void) {
    return 42;
}
