// CCCC_FLAGS: -Wsentinel
// CCCC_EXPECT_STDERR: not enough variable arguments to fit a sentinel
// A negative sentinel position must not walk off the end of the argument
// list (regression: target = nargs - 1 - (-1) = nargs, which is one past
// the last argument).
void foo(int a, ...) __attribute__((sentinel(-1)));
void foo(int a, ...) {}
int main(void) {
    foo(1, (void *)0);
    return 42;
}
