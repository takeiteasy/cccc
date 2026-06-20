// Self-test for the CCCC built-in testing framework.
// CCCC_FLAGS: --testing

[[cccc::test]]
void test_assert_true(void) {
    Assert(1 == 1);
    Assert(42 != 0);
}

[[cccc::test]]
void test_assert_eq(void) {
    AssertEq(1 + 1, 2);
    AssertEq(6 * 7, 42);
}

[[cccc::test]]
void test_assert_neq(void) {
    AssertNeq(1, 2);
    AssertNeq(0, 42);
}

[[cccc::test]]
void test_assert_null(void) {
    void *p = 0;
    AssertNull(p);
}

[[cccc::test]]
void test_assert_not_null(void) {
    int x = 42;
    AssertNotNull(&x);
}

[[cccc::test]]
void test_assert_streq(void) {
    AssertStrEq("hello", "hello");
    AssertStrEq("", "");
}

// Suite via attribute argument.
[[cccc::test(suite = "math")]]
void test_addition(void) {
    AssertEq(1 + 1, 2);
    AssertEq(10 + 32, 42);
}

[[cccc::test(suite = "math")]]
void test_subtraction(void) {
    AssertEq(5 - 3, 2);
    AssertEq(100 - 58, 42);
}

// Suite via pragma block.
#pragma cccc suite begin "strings"

[[cccc::test]]
void test_string_equality(void) {
    AssertStrEq("foo", "foo");
}

[[cccc::test]]
void test_string_empty(void) {
    AssertStrEq("", "");
}

#pragma cccc suite end

// --- Helper function tests ---

// A non-test helper called from within a test.
static int multiply(int a, int b) { return a * b; }

[[cccc::test]]
void test_calls_helper(void) {
    AssertEq(multiply(3, 7), 21);
    AssertEq(multiply(0, 100), 0);
    AssertEq(multiply(-2, 5), -10);
}

// Test calls a helper defined *after* it — forward declaration required in C.
static int square(int x);

[[cccc::test]]
void test_calls_forward(void) {
    AssertEq(square(5), 25);
    AssertEq(square(0), 0);
}

static int square(int x) { return x * x; }

// --- Edge cases ---

[[cccc::test]]
void test_local_struct(void) {
    struct { int x; int y; } pt;
    pt.x = 3;
    pt.y = 4;
    AssertEq(pt.x + pt.y, 7);
    AssertNotNull(&pt);
}

[[cccc::test]]
void test_multiple_assertions(void) {
    AssertEq(1 + 1, 2);
    AssertNeq(1, 2);
    AssertStrEq("hello", "hello");
    AssertNull((void *)0);
    int x = 42;
    AssertNotNull(&x);
}

// --- Negative tests: verify the compiler rejects invalid code ---

#pragma cccc suite begin "negative"

[[cccc::test(error = "undefined variable")]]
void test_neg_undeclared(void) {
    int x = totally_undeclared_variable;
}

[[cccc::test(error = "undefined variable")]]
void test_neg_undeclared_in_expr(void) {
    int arr[3];
    int x = arr[totally_undefined_index];
}

// Combined suite (pragma) + error attribute.
[[cccc::test(error = "undefined variable")]]
void test_neg_suite_and_error(void) {
    int y = also_does_not_exist;
}

#pragma cccc suite end

// ==========================================================================
// Ticket #331: name = "..." display name option
// ==========================================================================

[[cccc::test(name = "addition is commutative")]]
void test_addition_commutative(void) {
    AssertEq(1 + 2, 2 + 1);
    AssertEq(10 + 5, 5 + 10);
}

[[cccc::test(name = "named with suite", suite = "display_name_suite")]]
void test_named_with_suite(void) {
    Assert(1 == 1);
}

// ==========================================================================
// Ticket #329: global state reset between tests
// ==========================================================================

static int g_counter = 7;

[[cccc::test]]
void test_global_state_reset_a(void) {
    // Modify the global away from its initial value.
    g_counter = 42;
    AssertEq(g_counter, 42);
}

[[cccc::test]]
void test_global_state_reset_b(void) {
    // Global must be reset to its compile-time initial value (7), not just
    // zeroed — distinguishes a real restore from a memset.
    AssertEq(g_counter, 7);
}

// ==========================================================================
// Ticket #331: global setup and teardown (no suite, no name filter)
// ==========================================================================

static int g_setup_count = 0;

[[cccc::test_setup]]
void global_setup(void) {
    g_setup_count++;
}

#pragma cccc suite begin "setup_teardown"

[[cccc::test]]
void test_global_setup_ran(void) {
    // g_setup_count is incremented by global_setup before each test.
    // Since g_setup_count starts at 0 and global_setup runs before this test,
    // it should be 1.
    AssertEq(g_setup_count, 1);
}

[[cccc::test]]
void test_global_setup_runs_per_test(void) {
    // Each test gets a fresh snapshot (g_setup_count reset to 0) then
    // global_setup runs, so it should be 1 again.
    AssertEq(g_setup_count, 1);
}

#pragma cccc suite end

// ==========================================================================
// Ticket #331: per-test teardown
// ==========================================================================

static int g_teardown_marker = 0;

[[cccc::test_teardown]]
void global_teardown(void) {
    // This runs after each test. We can't easily observe it directly, but we
    // can verify it doesn't crash and the framework runs correctly.
    g_teardown_marker = 99;
}

[[cccc::test]]
void test_teardown_doesnt_crash(void) {
    // Just ensure setup/teardown cycle completes without issue.
    Assert(1 == 1);
}

// ==========================================================================
// Ticket #331: name-pattern setup/teardown
// ==========================================================================

static int g_pattern_ran = 0;

[[cccc::test_setup(name = "pattern_*")]]
void pattern_setup(void) {
    g_pattern_ran = 1;
}

[[cccc::test]]
void test_no_pattern_match(void) {
    // Function name "test_no_pattern_match" does not match "pattern_*",
    // so pattern_setup should NOT run for this test.
    AssertEq(g_pattern_ran, 0);
}

[[cccc::test(name = "pattern_match")]]
void test_pattern_match_fn(void) {
    // display name "pattern_match" matches "pattern_*".
    AssertEq(g_pattern_ran, 1);
}

// ==========================================================================
// Ticket #331: suite once-setup and once-teardown
// ==========================================================================

static int g_once_setup_count  = 0;
static int g_once_teardown_ran = 0;

[[cccc::test_setup(suite = "once_suite", once)]]
void once_suite_setup(void) {
    g_once_setup_count++;
}

[[cccc::test_teardown(suite = "once_suite", once)]]
void once_suite_teardown(void) {
    g_once_teardown_ran = 1;
}

#pragma cccc suite begin "once_suite"

[[cccc::test]]
void test_once_setup_ran(void) {
    // once-setup ran exactly once before this test (count = 1).
    AssertEq(g_once_setup_count, 1);
}

[[cccc::test]]
void test_once_setup_not_repeated(void) {
    // once-setup still 1 — it did not re-run for this second test.
    AssertEq(g_once_setup_count, 1);
}

#pragma cccc suite end

// ==========================================================================
// Tests for new assertion macros (AssertFalse, AssertGt, etc.)
// ==========================================================================

#pragma cccc suite begin "new_assertions"

[[cccc::test]]
void test_assert_false_works(void) {
    AssertFalse(0);
    AssertFalse(1 == 2);
}

[[cccc::test]]
void test_assert_gt_works(void) {
    AssertGt(10, 5);
    AssertGt(0, -1);
}

[[cccc::test]]
void test_assert_lt_works(void) {
    AssertLt(5, 10);
    AssertLt(-1, 0);
}

[[cccc::test]]
void test_assert_ge_works(void) {
    AssertGe(10, 10);
    AssertGe(10, 5);
}

[[cccc::test]]
void test_assert_le_works(void) {
    AssertLe(5, 5);
    AssertLe(5, 10);
}

[[cccc::test]]
void test_assert_within_works(void) {
    AssertWithin(2, 10, 9);
    AssertWithin(0, 42, 42);
}

[[cccc::test]]
void test_assert_streq_len_works(void) {
    AssertStrEqLen("hello", "hello world", 5);
    AssertStrEqLen("", "", 0);
}

[[cccc::test]]
void test_assert_true_false(void) {
    AssertTrue(1 == 1);
    AssertFalse(1 == 2);
}

[[cccc::test]]
void test_assert_true_msg_works(void) {
    AssertTrueMsg(1 == 1, "trivial truth");
}

[[cccc::test]]
void test_assert_eq_msg_works(void) {
    AssertEqMsg(42, 42, "the answer");
}

[[cccc::test]]
void test_assert_streq_msg_works(void) {
    AssertStrEqMsg("hello", "hello", "greeting");
}

#pragma cccc suite end

// ==========================================================================
// Ticket #339: per-test timeout via [[cccc::test(timeout = ...)]]
// ==========================================================================

[[cccc::test(timeout = 5000)]]
void test_per_test_timeout(void) {
    AssertEq(1 + 1, 2);
}

// ==========================================================================
// Ticket #340: code = N for negative tests (expected error count)
// ==========================================================================

#pragma cccc suite begin "error_count"

// Produces exactly 1 error (one undefined variable).
[[cccc::test(error = "undefined variable", error_count = 1)]]
void test_neg_one_error(void) {
    int x = totally_not_defined;
}

#pragma cccc suite end

// ==========================================================================
// Ticket #341: once + name_pat for setup/teardown
// ==========================================================================

static int g_once_namepat_count = 0;

[[cccc::test_setup(name = "once_namepat_*", once)]]
void once_namepat_setup(void) {
    g_once_namepat_count++;
}

[[cccc::test]]
void test_once_namepat_not_matched(void) {
    // Does NOT match "once_namepat_*", so once-namepat-setup should not fire here.
    AssertEq(g_once_namepat_count, 0);
}

[[cccc::test(name = "once_namepat_first")]]
void test_once_namepat_matched_first(void) {
    // First matching test: once-namepat-setup should have fired once.
    AssertEq(g_once_namepat_count, 1);
}

[[cccc::test(name = "once_namepat_second")]]
void test_once_namepat_matched_second(void) {
    // Second matching test: once-namepat-setup should still be 1 (did not re-fire).
    AssertEq(g_once_namepat_count, 1);
}

// ==========================================================================
// Ticket #344: nested sub-suites
// Tests verify that:
//   - #pragma cccc suite begin can be nested
//   - The composite suite path uses '/' as separator
//   - Closing inner block restores the outer path
//   - Tests outside all blocks have no suite
// ==========================================================================

#pragma cccc suite begin "parent"

[[cccc::test(suite = "parent/explicit")]]
void test_nested_attr_form(void) {
    // Attribute form 'suite = "a/b"' already worked; verify it still does.
    AssertEq(1, 1);
}

[[cccc::test]]
void test_nested_parent_only(void) {
    // In the "parent" pragma block; should have suite "parent".
    AssertEq(2, 2);
}

#pragma cccc suite begin "child"

[[cccc::test]]
void test_nested_child(void) {
    // In nested "parent"/"child" block; should have suite "parent/child".
    AssertEq(3, 3);
}

#pragma cccc suite begin "grandchild"

[[cccc::test]]
void test_nested_grandchild(void) {
    // Three levels deep: "parent/child/grandchild".
    AssertEq(4, 4);
}

#pragma cccc suite end  // end grandchild

[[cccc::test]]
void test_nested_back_to_child(void) {
    // Back in "parent/child" after closing grandchild.
    AssertEq(5, 5);
}

#pragma cccc suite end  // end child

[[cccc::test]]
void test_nested_back_to_parent(void) {
    // Back in "parent" after closing child.
    AssertEq(6, 6);
}

#pragma cccc suite end  // end parent

[[cccc::test]]
void test_nested_no_suite(void) {
    // Outside all suite blocks; no suite.
    AssertEq(7, 7);
}
