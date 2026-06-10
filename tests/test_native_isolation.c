// Native mode isolation test: each test runs in a forked child so global
// state mutations in one test are invisible to subsequent tests.
// CCCC_FLAGS: --testing

static int g_counter = 0;

[[cccc::test(mode = "native")]]
void test_isolation_a(void) {
    g_counter = 999;  // mutate global — should not affect test_isolation_b
    $assert_eq(g_counter, 999);
}

[[cccc::test(mode = "native")]]
void test_isolation_b(void) {
    // g_counter is still 0 because the child from test_isolation_a exited.
    $assert_eq(g_counter, 0);
}
