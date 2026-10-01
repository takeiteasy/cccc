// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: conflicting address spaces
[[cccc::kernel]] static void k([[cccc::global]] [[cccc::local]] int *g) {
    (void)g;
}
int main(void) {
    return 42;
}
