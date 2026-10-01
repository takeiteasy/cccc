// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: converting a pointer from the global to the private
[[cccc::kernel]] static void k([[cccc::global]] int *g) {
    int *p = g;
    (void)p;
}
int main(void) {
    return 42;
}
