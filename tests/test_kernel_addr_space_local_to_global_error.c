// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: converting a pointer from the local to the global
[[cccc::kernel]] static void k([[cccc::local]] int  *l,
                               [[cccc::global]] int *g) {
    g = l;
}
int main(void) {
    return 42;
}
