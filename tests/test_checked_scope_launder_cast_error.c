// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cast from an unchecked pointer to a checked pointer
// Checked regions: a bare cast from an unchecked pointer to a checked array
// pointer fabricates bounds.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(int n) {
    int *[[cccc::array, cccc::count(n)]] p =
        (int *[[cccc::array, cccc::count(n)]])g_raw;
    return p[0];
}

int main(void) {
    return 42;
}
