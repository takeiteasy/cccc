// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: generic pointer parameter on a kernel entry
[[cccc::kernel]] static void k([[cccc::generic]] int *p) {
    (void)p;
}
int main(void) {
    return 42;
}
