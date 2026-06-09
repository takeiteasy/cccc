/* CCCC_FLAGS: --std=c89 -Wpedantic */
/* CCCC_EXPECT_STDERR: warning: 'long long' is a C99 extension \[-Wpedantic\] */
long long x = 1;
int main(void) { return (int)x == 1 ? 42 : 1; }
