// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: argument may be null when passed to a parameter marked
// nonnull \(parameter 1\) Follow-up to #688 (#690): the call to maybe_null() is
// no longer required to be assigned to a local first -- an inline call used
// directly as the argument expression now also feeds NN_MAYBE via
// nn_state_of_expr().
int *maybe_null(int cond) {
    static int x = 0;
    if (cond)
        return &x;
    return 0;
}
void handle(int *p) __attribute__((nonnull));
void handle(int *p) {}
int main(void) {
    handle(maybe_null(1));
    return 42;
}
