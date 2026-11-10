// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected a parameter name in @template
@template(int) int f(void) {
    return 0;
}
int main(void) {
    return 42;
}
