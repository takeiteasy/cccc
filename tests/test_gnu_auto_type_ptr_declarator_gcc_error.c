// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: '__auto_type' requires a plain identifier as declarator
int main(void) {
    int          a = 1;
    __auto_type *p = &a;
    return *p;
}
