// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: names later parameter 'B'
@template(int A = B, int B = 1) int f(void) {
    return A;
}
int main(void) {
    return 42;
}
