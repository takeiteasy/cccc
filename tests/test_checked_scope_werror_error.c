// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror
// CCCC_EXPECT_STDERR: error: unchecked pointer local variable 'p'
// Checked regions: -Werror alone leaves a violation an error.

[[cccc::checked]]
void f(void) {
    int *p = 0;
    (void)p;
}

int main(void) {
    return 42;
}
