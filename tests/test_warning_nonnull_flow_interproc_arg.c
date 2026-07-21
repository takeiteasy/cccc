// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: argument may be null when passed to a parameter marked nonnull \(parameter 1\)
// Interprocedural extension (#688, follow-up to #687): maybe_null() has a
// provable null-returning path, so the whole-TU summary pass flags it.
// The result assigned to a local and passed to a nonnull param is "maybe
// null" evidence -- the ticket's own motivating example.
int *maybe_null(int cond) {
    static int x = 0;
    if (cond) return &x;
    return 0;
}
void handle(int *p) __attribute__((nonnull));
void handle(int *p) { }
int main(void) {
    int *p = maybe_null(1);
    handle(p);
    return 42;
}
