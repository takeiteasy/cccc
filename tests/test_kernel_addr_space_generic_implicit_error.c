// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: converting a pointer from the generic to the global
[[cccc::kernel]] static void k([[cccc::global]] int *g) {
    [[cccc::generic]] int *x = g;
    [[cccc::global]] int  *y = x;
    (void)y;
}
int main(void) {
    return 42;
}
