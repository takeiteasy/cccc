// CCCC_FLAGS: -Wsentinel
// CCCC_EXPECT_STDERR: not enough variable arguments to fit a sentinel
void foo(int a, ...) __attribute__((sentinel(1)));
void foo(int a, ...) {}
int main(void) {
    foo(1);
    return 42;
}
