// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: writing to constant memory
[[cccc::kernel]] static void k([[cccc::constant]] int *c) {
    c[0] = 1;
}
int main(void) {
    return 42;
}
