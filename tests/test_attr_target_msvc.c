// CCCC_FLAGS: --attr-target=msvc -E
// CCCC_EXPECT_STDOUT: __declspec\(align\(8\)\) __declspec\(noreturn\) void f\(void\);
// CCCC_EXPECT_STDOUT: __declspec\(deprecated\("x"\)\) int g;
// CCCC_EXPECT_STDOUT: __declspec\(dllexport\) int h;
// CCCC_REJECT_STDOUT: nodiscard|cold|unused|packed
// CCCC_EXPECT_STDERR: 'packed' has no __declspec form
@aligned(8) @noreturn void f(void);
@deprecated("x") int g;
@nodiscard @cold int k(void);
[[gnu::unused]] int u;
__attribute__((dllexport)) int h;
@packed struct S {
    int x;
};
int main(void) {
    return 42;
}
