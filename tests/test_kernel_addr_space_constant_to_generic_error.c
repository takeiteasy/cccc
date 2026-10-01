// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: converting a pointer from the constant to the generic
[[cccc::kernel]] static void k([[cccc::constant]] const int *c) {
    [[cccc::generic]] const int *x = c;
    (void)x;
}
int main(void) {
    return 42;
}
