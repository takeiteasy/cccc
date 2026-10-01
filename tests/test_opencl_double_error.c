// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: double or long double
kernel void k(global double *g) {
    g[0] = 1;
}
int main(void) {
    return 42;
}
