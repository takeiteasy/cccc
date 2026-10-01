// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: converting a pointer from the local to the global
static [[cccc::global]] int *r([[cccc::local]] int *l) {
    return l;
}
[[cccc::kernel]] static void k([[cccc::local]] int *l) {
    (void)r(l);
}
int main(void) {
    return 42;
}
