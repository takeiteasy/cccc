// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'N' has integer type and cannot bind 'D', which has floating type
@template(double D, double E) int f(void) {
    return 0;
}
@template(int N) @specialize(N, 1.0) int f(void) {
    return 1;
}
int main(void) {
    return 42;
}
