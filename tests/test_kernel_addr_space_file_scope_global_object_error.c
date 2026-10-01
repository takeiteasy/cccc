// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: global variable
[[cccc::global]] static int buf[4];
[[cccc::kernel]] static void k(void) {
    buf[0] = 1;
}
int main(void) {
    return 42;
}
