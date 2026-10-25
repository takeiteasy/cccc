// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter 'p' has no bounds in a checked region
// Checked regions: array-kind parameter with no bounds form.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(int *[[cccc::array]] p) {
    return p[0];
}

int main(void) {
    return 42;
}
