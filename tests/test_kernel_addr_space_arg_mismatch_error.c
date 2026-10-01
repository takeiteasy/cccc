// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: converting a pointer from the global to the local
static void h([[cccc::local]] int *l) {
    l[0] = 1;
}
[[cccc::kernel]] static void k(int *g) {
    h(g);
}
int main(void) {
    return 42;
}
