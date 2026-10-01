// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: converting a pointer from the global to the local
[[cccc::kernel]] static void k([[cccc::global]] int *g) {
    [[cccc::local]] int *l = (int *)g;
    (void)l;
}
int main(void) {
    return 42;
}
