// Ticket #344: --test-suite prefix match must not collide with names sharing
// the prefix string but not the '/' boundary.
// e.g. --test-suite=outer must NOT match suite "outerspace".
// CCCC_FLAGS: --testing --list-tests --test-suite=outer
// CCCC_EXPECT_STDOUT: suite: outer
// CCCC_REJECT_STDOUT: suite: outerspace

#pragma cccc suite begin "outer"

[[cccc::test]]
void test_prefix_guard_outer(void) { AssertEq(1, 1); }

#pragma cccc suite end

#pragma cccc suite begin "outerspace"

[[cccc::test]]
void test_prefix_guard_outerspace(void) { AssertEq(2, 2); }

#pragma cccc suite end
