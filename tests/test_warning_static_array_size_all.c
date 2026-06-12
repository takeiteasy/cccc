// CCCC_FLAGS: -Wall
// CCCC_EXPECT_STDERR: array argument has 3 element.*\[-Wstatic-array-size\]
void f(int a[static 10]) {}
int main(void) { int small[3]; f(small); return 42; }
