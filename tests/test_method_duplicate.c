// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: method .len. is already defined for this type
typedef struct { int x; } A;
[[cccc::method(A, len)]]
int a_len(A *self) { return 0; }
[[cccc::method(A, len)]]
int a_len2(A *self) { return 1; }
int main(void) { return 0; }
