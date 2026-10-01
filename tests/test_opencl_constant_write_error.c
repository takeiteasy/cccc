// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -x cl
// CCCC_EXPECT_STDERR: writing to constant memory
kernel void k(constant int *c) {
    c[0] = 1;
}
int main(void) {
    return 42;
}
