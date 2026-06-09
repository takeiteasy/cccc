// Self-test for the JCC built-in testing framework.
// JCC_FLAGS: --testing

[[jcc::test]]
void test_assert_true(void) {
    JCC_ASSERT(1 == 1);
    JCC_ASSERT(42 != 0);
}

[[jcc::test]]
void test_assert_eq(void) {
    JCC_ASSERT_EQ(1 + 1, 2);
    JCC_ASSERT_EQ(6 * 7, 42);
}

[[jcc::test]]
void test_assert_neq(void) {
    JCC_ASSERT_NEQ(1, 2);
    JCC_ASSERT_NEQ(0, 42);
}

[[jcc::test]]
void test_assert_null(void) {
    void *p = 0;
    JCC_ASSERT_NULL(p);
}

[[jcc::test]]
void test_assert_not_null(void) {
    int x = 42;
    JCC_ASSERT_NOT_NULL(&x);
}

[[jcc::test]]
void test_assert_streq(void) {
    JCC_ASSERT_STREQ("hello", "hello");
    JCC_ASSERT_STREQ("", "");
}
