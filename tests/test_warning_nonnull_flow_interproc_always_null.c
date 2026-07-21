// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: null value passed to a parameter marked nonnull \(parameter 1\)
// Follow-up to #688/#690/#693 (#692): always_null() has no non-null return
// path at all -- every reachable return is a literal null -- so the
// whole-TU summary now flags it always_returns_null, and nn_state_of_expr()
// promotes the call-site fact to NN_NULL instead of NN_MAYBE. This warns
// under plain -Wnonnull's message text (still requires -Wmaybe-nonnull to
// be passed too, since that's what gates the interprocedural pass itself).
int *always_null(void) {
    return 0;
}
void handle(int *p) __attribute__((nonnull));
void handle(int *p) { }
int main(void) {
    int *p = always_null();
    handle(p);
    return 42;
}
