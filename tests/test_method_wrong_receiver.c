// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must take the receiver type as its first parameter
typedef struct { int x; } A;
typedef struct { int x; } B;
[[cccc::method(A)]]
int f(B *self) { return 0; }
int main(void) { return 0; }
