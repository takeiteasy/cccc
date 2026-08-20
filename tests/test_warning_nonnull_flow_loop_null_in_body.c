// CCCC_FLAGS: -Wnonnull
// CCCC_EXPECT_STDERR: null value passed
// #689: a pointer that is null on *every* path through a loop body must
// still warn under plain -Wnonnull, not just -Wmaybe-nonnull.
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int *p = 0;
    for (int i = 0; i < 3; i++) {
        foo(p);
    }
    return 42;
}
