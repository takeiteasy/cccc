// EXPECT_COMPILE_ERROR
struct S { int x : -1; };
int main() { return 42; }
