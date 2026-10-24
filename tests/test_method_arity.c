// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: too many arguments|too few arguments
typedef struct { int x; } A;
[[cccc::method(A)]]
int f(A *self, int k) { return k; }
int main(void) { A a = {1}; return a.f(); }
