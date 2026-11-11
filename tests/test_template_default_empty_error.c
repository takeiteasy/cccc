// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected a default value in @template
@template(T, int N =) int f(void) {
    return N;
}
int main(void) {
    return 42;
}
