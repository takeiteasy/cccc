// CCCC_FLAGS: -Wnonnull
// CCCC_EXPECT_STDERR: null passed to a parameter marked nonnull \(parameter 1\)
// CCCC_REJECT_STDERR: parameter 2
void foo(int *a, int *b) __attribute__((nonnull(1)));
void foo(int *a, int *b) { }
int main(void) {
    foo((void*)0, (void*)0);
    return 42;
}
