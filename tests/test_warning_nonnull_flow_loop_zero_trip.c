// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// CCCC_REJECT_STDERR: null value passed
// #689: a `for` loop may run zero times, so even though the body
// unconditionally assigns a non-null value, the pre-loop null state still
// flows to the exit -- the result must be MAYBE, never definite NULL.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0;
    int *p = 0;
    int n = 0;
    for (int i = 0; i < n; i++) {
        p = &x;
    }
    foo(p);
    return 42;
}
