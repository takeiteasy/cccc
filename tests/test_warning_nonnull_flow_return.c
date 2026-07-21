// CCCC_FLAGS: -Wnonnull
// CCCC_EXPECT_STDERR: null value returned from function declared with 'returns_nonnull'
int *foo(void) __attribute__((returns_nonnull));
int *foo(void) {
    int *p = 0;
    return p;
}
int main(void) { return 42; }
