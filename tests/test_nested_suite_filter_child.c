// Ticket #344: verify --test-suite= selects exactly a sub-suite.
// --test-suite=outer/inner should include only "outer/inner" tests,
// NOT "outer" tests and NOT "unrelated" tests.
// CCCC_FLAGS: --testing --list-tests --test-suite=outer/inner
// CCCC_EXPECT_STDOUT: suite: outer/inner
// CCCC_REJECT_STDOUT: suite: outer\]
// CCCC_REJECT_STDOUT: suite: unrelated

// Also verifies the prefix-collision guard: --test-suite=outer must NOT
// match a suite named "outerspace".
// (See test_nested_suite_prefix_guard.c for that case.)

#pragma cccc suite begin "outer"

[[cccc::test]]
void test_subfilter_outer_only(void) {
    AssertEq(1, 1);
}

#pragma cccc suite begin "inner"

[[cccc::test]]
void test_subfilter_inner_a(void) {
    AssertEq(2, 2);
}

[[cccc::test]]
void test_subfilter_inner_b(void) {
    AssertEq(3, 3);
}

#pragma cccc suite end // end inner

#pragma cccc suite end // end outer

#pragma cccc suite begin "unrelated"

[[cccc::test]]
void test_subfilter_unrelated(void) {
    AssertEq(4, 4);
}

#pragma cccc suite end
