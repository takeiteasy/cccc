// CCCC_FLAGS: --std=c23 -E
// CCCC_EXPECT_STDOUT: \[\[nodiscard\]\] int f\(void\);
// CCCC_EXPECT_STDOUT: __attribute__\(\(packed\)\) struct S
@nodiscard int f(void);
@packed struct S {
    int x;
};
int main(void) {
    return 42;
}
