// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: local-memory object outside a kernel entry
static void h(void) {
    [[cccc::local]] int t[4];
    t[0] = 1;
}
[[cccc::kernel]] static void k(void) {
    h();
}
int main(void) {
    return 42;
}
