// CCCC_FLAGS: -Wnonnull
// CCCC_EXPECT_STDERR: null value passed to a parameter marked nonnull
// \(parameter 1\)
void foo(int *p) __attribute__((nonnull));
void foo(int *p) {}
int main(void) {
    int *p = 0;
    foo(p);
    return 42;
}
