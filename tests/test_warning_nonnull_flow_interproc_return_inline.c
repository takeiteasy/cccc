// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: value may be null when returned from function declared with 'returns_nonnull'
// Follow-up to #688 (#690): a bare call used directly as the return
// expression (not first assigned to a local) now also feeds NN_MAYBE via
// nn_state_of_expr() in nn_check_return().
int *maybe_null(int cond) {
    static int x = 0;
    if (cond) return &x;
    return 0;
}
int *wrap(int cond) __attribute__((returns_nonnull));
int *wrap(int cond) {
    return maybe_null(cond);
}
int main(void) { return 42; }
