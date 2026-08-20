// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: argument may be null when passed to a parameter marked
// nonnull \(parameter 1\) The test that justifies moving check_nonnull_flow()
// to run post-parse
// (#688): use() is defined *before* maybe_null(), so under the old
// per-function-at-parse-time scheme maybe_null()'s summary would not yet
// exist when use() was checked. A prototype makes the forward reference
// legal; the whole-TU summary pass (run after every function is parsed)
// still catches it.
int *maybe_null(int cond);
void handle(int *p) __attribute__((nonnull));
void handle(int *p) {}
void use(void) {
    int *p = maybe_null(1);
    handle(p);
}
int *maybe_null(int cond) {
    static int x = 0;
    if (cond)
        return &x;
    return 0;
}
int main(void) {
    use();
    return 42;
}
