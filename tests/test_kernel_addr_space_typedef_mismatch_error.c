// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: converting a pointer from the global to the private
typedef [[cccc::global]] int GInt;
[[cccc::kernel]] static void k(GInt *g) {
    int *p = g;
    (void)p;
}
int main(void) {
    return 42;
}
