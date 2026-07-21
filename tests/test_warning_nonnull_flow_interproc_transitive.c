// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: argument may be null when passed to a parameter marked nonnull \(parameter 1\)
// Follow-up to #690 (#693): relay() has no literal null return itself -- its
// only return path is a call to maybe_null(), which is flagged. The
// fixpoint loop in check_may_return_null_summaries() now propagates
// may_return_null through relay() transitively.
int *maybe_null(int cond) {
    static int x = 0;
    if (cond) return &x;
    return 0;
}
int *relay(int cond) {
    return maybe_null(cond);
}
void handle(int *p) __attribute__((nonnull));
void handle(int *p) { }
int main(void) {
    int *p = relay(1);
    handle(p);
    return 42;
}
