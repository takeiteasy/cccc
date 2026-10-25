// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: implicit conversion from an unchecked pointer
// Checked regions: assigning an unchecked pointer to a checked one.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(void) {
    int *[[cccc::single]] p = &g_x;
    p                       = g_raw;
    return *p;
}

int main(void) {
    return 42;
}
