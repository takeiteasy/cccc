// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: implicit conversion from an unchecked pointer
// Checked regions: passing an unchecked pointer for a checked parameter.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

int take(int *[[cccc::array, cccc::count(n)]] p, int n);

[[cccc::checked]]
int f(int n) {
    return take(g_raw, n);
}

int main(void) {
    return 42;
}
