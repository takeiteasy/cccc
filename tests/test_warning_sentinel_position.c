// CCCC_FLAGS: -Wsentinel
// CCCC_EXPECT_STDERR: missing sentinel in function call
// sentinel(1) allows one trailing non-sentinel arg before the NULL; the
// NULL here is in the wrong (last) slot, so the required position (one
// before the end) is not a literal NULL.
void foo(int a, ...) __attribute__((sentinel(1)));
void foo(int a, ...) { }
int main(void) {
    foo(1, 2, 3, (void*)0);
    return 42;
}
