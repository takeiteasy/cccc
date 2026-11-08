// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDERR: '!=' does not apply to a nested compound literal

struct In {
    int a;
};

struct Out {
    struct In in;
};

[[cccc::test(return = (struct Out){.in != {.a = 1}})]] struct Out
    test_nested_ne(void) {
    return (struct Out){{1}};
}
