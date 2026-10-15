// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not a compile-time constant|not a constant expression
// A comma operator with a side effect on the left is not a constant expression.
int f(void);
_Static_assert((f(), 1), "x");
int main(void) { return 42; }
