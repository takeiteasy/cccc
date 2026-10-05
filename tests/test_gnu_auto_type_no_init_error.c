// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: '__auto_type' requires an initialized data declaration
int main(void) {
    __auto_type t;
    t = 1;
    return t;
}
