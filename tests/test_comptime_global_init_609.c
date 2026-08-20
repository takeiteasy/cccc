// EXPECT_COMPILE_ERROR
// Regression test for ticket #609: using a [[cccc::comptime]] function in a
// global variable initializer no longer crashes with SIGSEGV; it now produces
// a clean compile error (global initializers require compile-time constants,
// not macro-expanded values).
[[cccc::comptime]]
int ct_sum(int a, int b) {
    return a + b;
}

static const int G = ct_sum(20, 22);

int main(void) {
    return G;
}
