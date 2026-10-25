// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: needs a struct or union operand
[[cccc::operator(-)]]
int neg(int a) {
    return -a;
}
int main(void) {
    return 0;
}
