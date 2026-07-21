// CCCC_FLAGS: -Wattributes -Wsentinel
// CCCC_EXPECT_STDERR: sentinel attribute only applies to variadic functions
// CCCC_REJECT_STDERR: not enough variable arguments
// sentinel on a non-variadic function is misapplied; the declaration-time
// warning fires and the per-call-site "not enough variable arguments" guard
// must not also fire (#696).
void foo(int a) __attribute__((sentinel));
void foo(int a) { }
int main(void) {
    foo(1);
    return 42;
}
