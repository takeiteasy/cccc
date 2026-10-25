// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Wno-checked-regions
// CCCC_EXPECT_STDERR: error: unchecked pointer local variable 'p'
// Checked regions: -Wno-checked-regions does not silence a violation.

[[cccc::checked]]
void f(void) {
    int *p = 0;
    (void)p;
}

int main(void) {
    return 42;
}
