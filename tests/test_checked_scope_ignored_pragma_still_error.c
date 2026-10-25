// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: unchecked pointer local variable 'p'
// Checked regions: #pragma GCC diagnostic ignored does not silence a violation.

#pragma GCC diagnostic ignored "-Wchecked-regions"

[[cccc::checked]]
void f(void) {
    int *p = 0;
    (void)p;
}

int main(void) {
    return 42;
}
