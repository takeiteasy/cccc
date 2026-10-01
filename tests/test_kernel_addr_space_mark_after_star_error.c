// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: write 'global' before the '\*'
[[cccc::kernel]] static void k(int *[[cccc::global]] g) {
    (void)g;
}
int main(void) {
    return 42;
}
