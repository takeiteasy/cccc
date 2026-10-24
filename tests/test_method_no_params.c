// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must take the receiver type as its first parameter
typedef struct { int x; } A;
[[cccc::method(A)]]
int f(void) { return 0; }
int main(void) { return 0; }
