// CCCC_FLAGS: --testing=native --test-suite=alpha
// CCCC_EXPECT_STDOUT: 1\.\.2
// CCCC_EXPECT_STDOUT: ^ok 1 - in_alpha
// CCCC_EXPECT_STDOUT: ^ok 2 - in_inner
// CCCC_REJECT_STDOUT: in_beta
// --test-suite=NAME selects the suite and its sub-suites.
#pragma cccc suite begin "alpha"
[[cccc::test]]
void in_alpha(void) {}

#pragma cccc suite begin "inner"
[[cccc::test]]
void in_inner(void) {}
#pragma cccc suite end
#pragma cccc suite end

#pragma cccc suite begin "beta"
[[cccc::test]]
void in_beta(void) {
    Assert(0);
}
#pragma cccc suite end
