// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: receives local memory
static void h(int *p) {
    p[0] = 1;
}
[[cccc::kernel]] static void a(int *g) {
    h(g);
}
[[cccc::kernel]] static void b([[cccc::local]] int *l) {
    h(l);
}
int main(void) {
    return 42;
}
