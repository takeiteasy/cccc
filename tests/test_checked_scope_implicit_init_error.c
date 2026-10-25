// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: implicit conversion from an unchecked pointer
// Checked regions: initialising a checked array pointer from an unchecked one.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(int n) {
    int *[[cccc::array, cccc::count(n)]] p = g_raw;
    return p[0];
}

int main(void) {
    return 42;
}
