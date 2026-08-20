// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
// A null assignment in one switch case must not bleed into a later case --
// the switch head dispatches directly into each case, so case N doesn't
// necessarily execute after case N-1. See #679.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int  x = 0;
    int *p = &x;
    switch (x) {
        case 0:
            p = 0;
            break;
        case 1:
            foo(p);
            break;
    }
    return 42;
}
