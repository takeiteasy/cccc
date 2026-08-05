// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// #689: a maybe-null pattern inside a `for` loop's body now warns under
// -Wmaybe-nonnull -- this is the ticket's own example.
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
