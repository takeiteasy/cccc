// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: value may be null when returned from function declared
// with 'returns_nonnull' Follow-up to #690 (#693): same transitive chain as
// test_warning_nonnull_flow_interproc_transitive.c, but relay() is defined
// before maybe_null() in source order, and the chain is three hops deep
// (wrap -> relay -> maybe_null), feeding into an inline return expression
// (#690) rather than an assigned local. Exercises that the fixpoint loop
// converges regardless of source order or chain depth.
int *relay(int cond);
int *maybe_null(int cond) {
    static int x = 0;
    if (cond)
        return &x;
    return 0;
}
int *relay(int cond) {
    return maybe_null(cond);
}
int *wrap(int cond) __attribute__((returns_nonnull));
int *wrap(int cond) {
    return relay(cond);
}
int main(void) {
    return 42;
}
