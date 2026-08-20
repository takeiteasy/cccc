// CCCC_FLAGS: -Wnodiscard --std=c23
// CCCC_EXPECT_STDERR: warning: ignoring return value of function declared with
// 'nodiscard'
int [[nodiscard]] foo(void) {
    return 42;
}
int main(void) {
    foo();
    return 42;
}