// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDOUT: ^int a = 0;
// CCCC_EXPECT_STDOUT: ^int b = 1;
// CCCC_EXPECT_STDOUT: ^int c = 0;
// CCCC_EXPECT_STDOUT: ^int d = 1;
// CCCC_EXPECT_STDOUT: ^int e = 0;
// CCCC_EXPECT_STDOUT: ^int f = 0;
#if __has_attribute(overloadable)
#error "gcc has no overloadable"
#endif
int a = __has_attribute(overloadable);
int b = __has_attribute(optimize);
int c = __has_attribute(enable_if);
int d = __has_attribute(unavailable);
int e = __has_c_attribute(no_unique_address);
int f = __has_c_attribute(clang::overloadable);
int main(void) {
    return 42;
}
