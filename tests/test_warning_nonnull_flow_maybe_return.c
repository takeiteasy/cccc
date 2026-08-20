// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: value may be null when returned from function declared
// with 'returns_nonnull' Same maybe-null join as
// test_warning_nonnull_flow_maybe_arg.c, but on the returns_nonnull
// return-value path (#687).
int *foo(int cond) __attribute__((returns_nonnull));
int *foo(int cond) {
    static int x = 0;
    int       *p = 0;
    if (cond)
        p = &x;
    return p;
}
int main(void) {
    return 42;
}
