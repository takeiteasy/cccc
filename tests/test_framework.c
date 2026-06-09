// Self-test for the CCCC built-in testing framework.
// CCCC_FLAGS: --testing

[[cccc::test]]
void test_assert_true(void) {
    CCCC_ASSERT(1 == 1);
    CCCC_ASSERT(42 != 0);
}

[[cccc::test]]
void test_assert_eq(void) {
    CCCC_ASSERT_EQ(1 + 1, 2);
    CCCC_ASSERT_EQ(6 * 7, 42);
}

[[cccc::test]]
void test_assert_neq(void) {
    CCCC_ASSERT_NEQ(1, 2);
    CCCC_ASSERT_NEQ(0, 42);
}

[[cccc::test]]
void test_assert_null(void) {
    void *p = 0;
    CCCC_ASSERT_NULL(p);
}

[[cccc::test]]
void test_assert_not_null(void) {
    int x = 42;
    CCCC_ASSERT_NOT_NULL(&x);
}

[[cccc::test]]
void test_assert_streq(void) {
    CCCC_ASSERT_STREQ("hello", "hello");
    CCCC_ASSERT_STREQ("", "");
}

// Suite via attribute argument.
[[cccc::test(suite = "math")]]
void test_addition(void) {
    CCCC_ASSERT_EQ(1 + 1, 2);
    CCCC_ASSERT_EQ(10 + 32, 42);
}

[[cccc::test(suite = "math")]]
void test_subtraction(void) {
    CCCC_ASSERT_EQ(5 - 3, 2);
    CCCC_ASSERT_EQ(100 - 58, 42);
}

// Suite via pragma block.
#pragma cccc suite begin "strings"

[[cccc::test]]
void test_string_equality(void) {
    CCCC_ASSERT_STREQ("foo", "foo");
}

[[cccc::test]]
void test_string_empty(void) {
    CCCC_ASSERT_STREQ("", "");
}

#pragma cccc suite end

// --- Helper function tests ---

// A non-test helper called from within a test.
static int multiply(int a, int b) { return a * b; }

[[cccc::test]]
void test_calls_helper(void) {
    CCCC_ASSERT_EQ(multiply(3, 7), 21);
    CCCC_ASSERT_EQ(multiply(0, 100), 0);
    CCCC_ASSERT_EQ(multiply(-2, 5), -10);
}

// Test calls a helper defined *after* it — forward declaration required in C.
static int square(int x);

[[cccc::test]]
void test_calls_forward(void) {
    CCCC_ASSERT_EQ(square(5), 25);
    CCCC_ASSERT_EQ(square(0), 0);
}

static int square(int x) { return x * x; }

// --- Edge cases ---

[[cccc::test]]
void test_local_struct(void) {
    struct { int x; int y; } pt;
    pt.x = 3;
    pt.y = 4;
    CCCC_ASSERT_EQ(pt.x + pt.y, 7);
    CCCC_ASSERT_NOT_NULL(&pt);
}

[[cccc::test]]
void test_multiple_assertions(void) {
    CCCC_ASSERT_EQ(1 + 1, 2);
    CCCC_ASSERT_NEQ(1, 2);
    CCCC_ASSERT_STREQ("hello", "hello");
    CCCC_ASSERT_NULL((void *)0);
    int x = 42;
    CCCC_ASSERT_NOT_NULL(&x);
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
    CCCC_ASSERT_EQ(1 + 2, 2 + 1);
    CCCC_ASSERT_EQ(10 + 5, 5 + 10);
}

[[cccc::test(name = "named with suite", suite = "display_name_suite")]]
void test_named_with_suite(void) {
    CCCC_ASSERT(1 == 1);
}

// ==========================================================================
// Ticket #329: global state reset between tests
// ==========================================================================

static int g_counter = 7;

[[cccc::test]]
void test_global_state_reset_a(void) {
    // Modify the global away from its initial value.
    g_counter = 42;
    CCCC_ASSERT_EQ(g_counter, 42);
}

[[cccc::test]]
void test_global_state_reset_b(void) {
    // Global must be reset to its compile-time initial value (7), not just
    // zeroed — distinguishes a real restore from a memset.
    CCCC_ASSERT_EQ(g_counter, 7);
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
    CCCC_ASSERT_EQ(g_setup_count, 1);
}

[[cccc::test]]
void test_global_setup_runs_per_test(void) {
    // Each test gets a fresh snapshot (g_setup_count reset to 0) then
    // global_setup runs, so it should be 1 again.
    CCCC_ASSERT_EQ(g_setup_count, 1);
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
    CCCC_ASSERT(1 == 1);
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
    CCCC_ASSERT_EQ(g_pattern_ran, 0);
}

[[cccc::test(name = "pattern_match")]]
void test_pattern_match_fn(void) {
    // display name "pattern_match" matches "pattern_*".
    CCCC_ASSERT_EQ(g_pattern_ran, 1);
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
    CCCC_ASSERT_EQ(g_once_setup_count, 1);
}

[[cccc::test]]
void test_once_setup_not_repeated(void) {
    // once-setup still 1 — it did not re-run for this second test.
    CCCC_ASSERT_EQ(g_once_setup_count, 1);
}

#pragma cccc suite end
