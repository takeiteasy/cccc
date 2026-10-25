// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: warning: unchecked pointer local variable 'demoted'
// CCCC_EXPECT_STDERR: error: unchecked pointer local variable 'enforced'
// Checked regions: #pragma GCC diagnostic warning "-Wchecked-regions" demotes
// the violations in its extent; a violation outside it is still an error.

#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wchecked-regions"
[[cccc::checked]]
void soft(void) {
    int *demoted = 0;
    (void)demoted;
}
#pragma GCC diagnostic pop

[[cccc::checked]]
void hard(void) {
    int *enforced = 0;
    (void)enforced;
}

int main(void) {
    return 42;
}
