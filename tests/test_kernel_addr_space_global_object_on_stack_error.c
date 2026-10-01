// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: global-memory object on the stack
[[cccc::kernel]] static void k(int *o) {
    [[cccc::global]] int x = 1;
    o[0]                   = x;
}
int main(void) {
    return 42;
}
