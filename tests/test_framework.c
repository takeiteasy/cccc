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
