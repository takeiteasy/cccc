// CCCC_FLAGS: --attr-target=msvc -E
// CCCC_EXPECT_STDOUT: __declspec\(nodiscard\) int f\(void\);
// CCCC_EXPECT_STDOUT: __declspec\(packed\) struct S
@nodiscard int f(void);
@packed struct S {
    int x;
};
int main(void) {
    return 42;
}
