// CCCC_FLAGS: -Wnonnull
// CCCC_EXPECT_STDERR: null value returned from function declared with
// 'returns_nonnull' #689: an empty loop must not weaken a provably-null value
// flowing into a `returns_nonnull` return -- the loop's fixpoint converges
// immediately (nothing in the body touches p) and the null state survives to
// the return.
int *bar(void) __attribute__((returns_nonnull));
int *bar(void) {
    int *p = 0;
    for (int i = 0; i < 3; i++) {
    }
    return p;
}
int main(void) {
    return 42;
}
