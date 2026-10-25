// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: implicit conversion from an unchecked pointer
// Checked regions: an allocator result needs an assume/dynamic cast.

extern void *malloc(unsigned long);
int *g_raw;
int  g_x;

[[cccc::checked]]
int f(void) {
    int *[[cccc::single]] p = malloc(4);
    return *p;
}

int main(void) {
    return 42;
}
