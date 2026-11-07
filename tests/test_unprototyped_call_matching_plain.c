// CCCC_FLAGS: --std=c17 -m
// CCCC_EXPECT_STDOUT: return g\(40, 2\);
// CCCC_REJECT_STDOUT: \(\*\)\(
int g();

int main(void) {
    return g(40, 2);
}
int g(int a, int b) {
    return a + b;
}
