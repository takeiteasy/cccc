// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// False-positive guard for the #688 interprocedural summary: never_null()
// has no literal-null return path (both branches return an address), so it
// must not be flagged may_return_null, and no warning should fire.
int *never_null(int cond) {
    static int x = 0, y = 0;
    if (cond) return &x;
    return &y;
}
void handle(int *p) __attribute__((nonnull));
void handle(int *p) { }
int main(void) {
    int *p = never_null(1);
    handle(p);
    return 42;
}
