// CCCC_FLAGS: -Wnodiscard
// CCCC_EXPECT_STDERR: warning: ignoring return value of function declared with 'nodiscard'
int foo(void) __attribute__((warn_unused_result));
int foo(void) { return 42; }
int main(void) { foo(); return 42; }
