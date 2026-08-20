// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// Follow-up to #688/#690/#693 (#692): always_null() is definitely null on
// every path, so it's now classified NN_NULL rather than NN_MAYBE -- same
// convention as an unconditionally-null local variable (see
// test_warning_nonnull_flow.c), which also stays silent under
// -Wmaybe-nonnull alone and needs -Wnonnull to fire. Companion to
// test_warning_nonnull_flow_interproc_always_null.c, which passes both
// flags and does warn.
int *always_null(void) {
    return 0;
}
void handle(int *p) __attribute__((nonnull));
void handle(int *p) {}
int main(void) {
    int *p = always_null();
    handle(p);
    return 42;
}
