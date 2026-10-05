// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot combine '__auto_type' with other type specifiers
int main(void) {
    int __auto_type t = 1;
    return t;
}
