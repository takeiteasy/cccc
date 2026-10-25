// CCCC_FLAGS: -Werror -Wno-error=checked-regions
// CCCC_EXPECT_STDERR: warning: unchecked pointer local variable 'p'
// Checked regions: -Wno-error=checked-regions wins over a global -Werror.

[[cccc::checked]]
void f(void) {
    int *p = 0;
    (void)p;
}

int main(void) {
    f();
    return 42;
}
