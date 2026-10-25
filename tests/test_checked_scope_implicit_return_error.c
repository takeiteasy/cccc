// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: implicit conversion from an unchecked pointer
// Checked regions: returning an unchecked pointer as a checked one.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]] int *[[cccc::single]] f(void) {
    return g_raw;
}

int main(void) {
    return 42;
}
