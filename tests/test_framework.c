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
