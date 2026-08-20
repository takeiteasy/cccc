// CCCC_FLAGS: -Wnonnull
// CCCC_REJECT_STDERR: nonnull
// A pointer whose address has been taken (and could have been reassigned
// through that alias) must never be tracked -- see #679.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
void set_it(int **out) {}
int main(void) {
    int *p = 0;
    set_it(&p);
    foo(p);
    return 42;
}
