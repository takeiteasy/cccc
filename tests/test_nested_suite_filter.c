// Ticket #344: verify --test-suite= hierarchical filtering for nested sub-suites.
// Runs with --list-tests so no tests actually execute; stdout is the test list.
// CCCC_FLAGS: --testing --list-tests --test-suite=outer
// CCCC_EXPECT_STDOUT: (?s)suite: outer.*suite: outer/inner
// CCCC_REJECT_STDOUT: suite: unrelated

// Three suites: "outer", "outer/inner", and "unrelated".
// With --test-suite=outer the filter should include "outer" and "outer/inner"
// but exclude "unrelated".

#pragma cccc suite begin "outer"

[[cccc::test]]
void test_filter_outer_a(void) { AssertEq(1, 1); }

[[cccc::test]]
void test_filter_outer_b(void) { AssertEq(2, 2); }

#pragma cccc suite begin "inner"

[[cccc::test]]
void test_filter_inner_a(void) { AssertEq(3, 3); }

[[cccc::test]]
void test_filter_inner_b(void) { AssertEq(4, 4); }

#pragma cccc suite end  // end inner

#pragma cccc suite end  // end outer

#pragma cccc suite begin "unrelated"

[[cccc::test]]
void test_filter_unrelated(void) { AssertEq(5, 5); }

#pragma cccc suite end
