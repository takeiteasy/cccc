// CCCC_FLAGS: -Wnonnull -Wmaybe-nonnull
// CCCC_EXPECT_STDERR: may be null
// #689: `continue` skips the assignment on some iterations, so the
// continue's accumulated env (still null, from before the assignment) must
// feed back into the loop header and ultimately the exit -- MAYBE.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) {
    int x = 0;
    int c = 0;
    int *p = 0;
    for (int i = 0; i < 3; i++) {
        if (c) continue;
        p = &x;
    }
    foo(p);
    return 42;
}
