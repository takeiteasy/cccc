// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: private pointer parameter on a kernel entry
[[cccc::kernel]] static void k([[cccc::private]] int *p) {
    (void)p;
}
int main(void) {
    return 42;
}
