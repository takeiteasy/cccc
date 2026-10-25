// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cast from an unchecked pointer to a checked pointer
// Checked regions: the cast ban covers a single target too.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(void) {
    int *[[cccc::single]] p = (int *[[cccc::single]])g_raw;
    return *p;
}

int main(void) {
    return 42;
}
