// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: int t\(void\)
// CCCC_REJECT_STDOUT: cccc::
// CCCC_REJECT_STDOUT: __test
__test__ int t(void) { return 0; }
[[cccc::test]] int u(void) { return 0; }
int main(void) { return 42; }
