// EXPECT_COMPILE_ERROR
struct S { unsigned char x : 9; };
int main() { return 42; }
