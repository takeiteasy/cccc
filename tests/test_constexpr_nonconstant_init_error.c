// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c23
// CCCC_EXPECT_STDERR: constexpr initializer is not a constant expression
int main(void) {
    int x = 1;
    constexpr int n = x;
    return n;
}
