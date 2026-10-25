// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked pointer local variable .p.
// Checked regions: a C23 auto local deduced as an unchecked pointer.

int *g_raw;

[[cccc::checked]]
int f(void) {
    auto p = g_raw;
    return p == 0;
}

int main(void) {
    return 42;
}
