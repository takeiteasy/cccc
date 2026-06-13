// CCCC_FLAGS: --std=c11 -E
// CCCC_EXPECT_STDOUT: __attribute__\(\(nodiscard\)\) int f\(void\);
// CCCC_EXPECT_STDOUT: __attribute__\(\(packed\)\) struct S
// CCCC_REJECT_STDOUT: \[\[nodiscard\]\]
@nodiscard int f(void);
@packed struct S { int x; };
int main(void) { return 42; }
