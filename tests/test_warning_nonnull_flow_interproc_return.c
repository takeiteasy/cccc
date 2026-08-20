// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: value may be null when returned from function declared
// with 'returns_nonnull' Same interprocedural summary fact as
// test_warning_nonnull_flow_interproc_arg.c
// (#688), but flowing into a returns_nonnull function's own return path.
int *maybe_null(int cond) {
    static int x = 0;
    if (cond)
        return &x;
    return 0;
}
int *wrap(int cond) __attribute__((returns_nonnull));
int *wrap(int cond) {
    int *p = maybe_null(cond);
    return p;
}
int main(void) {
    return 42;
}
