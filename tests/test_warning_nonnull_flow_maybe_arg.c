// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: argument may be null when passed to a parameter marked
// nonnull \(parameter 1\) Real per-branch merge dataflow (#687): p is null
// before the branch and only conditionally reassigned to non-null, so after the
// join it is "maybe null" -- opt-in -Wmaybe-nonnull warns here where plain
// -Wnonnull does not (see test_warning_nonnull_flow_maybe.c).
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = 0;
    if (x == 0)
        p = &x;
    foo(p);
    return 42;
}
