// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror=auto-declarator
// CCCC_EXPECT_STDERR: '__auto_type' may only be used with a single declarator
int main(void) {
    int         a = 1;
    __auto_type x = a, y = a;
    return x + y;
}
