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
