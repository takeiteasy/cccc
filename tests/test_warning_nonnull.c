// CCCC_FLAGS: -Wnonnull
// CCCC_EXPECT_STDERR: null passed to a parameter marked nonnull \(parameter 1\)
void foo(int *p) __attribute__((nonnull));
void foo(int *p) { }
int main(void) { foo((void*)0); return 42; }
