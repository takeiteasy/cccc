// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method receiver type must be a struct or union
[[cccc::static_method(int, make)]]
int int_make(void) {
    return 1;
}
int main(void) {
    return 0;
}
