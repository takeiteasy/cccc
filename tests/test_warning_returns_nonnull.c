// CCCC_FLAGS: -Wnonnull
// CCCC_EXPECT_STDERR: null returned from function declared with 'returns_nonnull'
int *foo(void) __attribute__((returns_nonnull));
int *foo(void) { return 0; }
int main(void) { foo(); return 42; }
