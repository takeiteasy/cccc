// CCCC_FLAGS: --std=c17 -m
// CCCC_EXPECT_STDOUT: int \(\*fp\)\(\);
// CCCC_EXPECT_STDOUT: \(int \(\*\)\(\)\)g
// CCCC_EXPECT_STDOUT: int t\(\);
// CCCC_EXPECT_STDOUT: double \(?sqrt\)?\(\);
// CCCC_EXPECT_STDOUT: int e\(\) \{
// CCCC_EXPECT_STDOUT: int g\(double d, int i\) \{
// CCCC_REJECT_STDOUT: \(\.\.\.\)
// CCCC_REJECT_STDOUT: , \.\.\.\)
double sqrt();
int (*fp)();
typedef int t();
int g();

int e() {
    return 2;
}
int g(double d, int i) {
    return (int)d + i;
}

int main(void) {
    t *tp = 0;
    fp    = g;
    return fp(2.5, 40) + e() - 2 + (tp != 0);
}
