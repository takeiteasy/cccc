// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: no such member .g.
typedef struct { int x; } A;
[[cccc::method(A)]]
int f(A *self) { return 0; }
int main(void) { A a = {1}; return a.g(); }
