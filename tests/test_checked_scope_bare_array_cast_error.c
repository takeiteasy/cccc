// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cast to an array pointer with no bounds
// Checked regions: array-kind cast type-name with no bounds form.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(int *[[cccc::array, cccc::count(1)]] s) {
    int *[[cccc::array, cccc::count(1)]] a = (int *[[cccc::array]])s;
    return a[0];
}

int main(void) {
    return 42;
}
