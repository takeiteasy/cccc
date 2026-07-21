// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// Both branches assign a definitely-non-null value, so the join stays
// NN_NONNULL -- no false positive from the new per-branch merge (#687).
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0, y = 0;
    int *p;
    int cond = 1;
    if (cond) p = &x; else p = &y;
    foo(p);
    return 42;
}
