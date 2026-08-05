// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// CCCC_REJECT_STDERR: may be null[\s\S]*may be null
// #689: exactly-once emission -- the loop's fixpoint iterates several times
// internally (all quiet), and only the single final reporting pass may emit
// a diagnostic for this one call site.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0;
    int *p = 0;
    for (int i = 0; i < 3; i++) {
        if (i == 1) p = &x;
    }
    foo(p);
    return 42;
}
