// --attr-target=gnu prints standard attributes by their GNU names in C23 mode.
// CCCC_FLAGS: --attr-target=gnu -E
// CCCC_EXPECT_STDOUT: __attribute__\(\(warn_unused_result\)\) int f\(void\);
// CCCC_EXPECT_STDOUT: __attribute__\(\(warn_unused_result\)\) int g\(void\);
// CCCC_EXPECT_STDOUT: __attribute__\(\(unused\)\) static int h;
// CCCC_EXPECT_STDOUT: __attribute__\(\(deprecated\("old"\)\)\) int d\(void\);
// CCCC_EXPECT_STDOUT: __attribute__\(\(warn_unused_result, deprecated, unused\)\) int m\(void\);
// CCCC_REJECT_STDOUT: nodiscard
// CCCC_REJECT_STDOUT: maybe_unused
// CCCC_REJECT_STDOUT: no_unique_address
// CCCC_REJECT_STDOUT: \[\[
[[nodiscard]] int f(void);
[[nodiscard("why")]] int g(void);
[[maybe_unused]] static int h;
[[deprecated("old")]] int d(void);
[[nodiscard, deprecated, maybe_unused]] int m(void);
struct S {
    [[no_unique_address]] int x;
};
int main(void) {
    return 42;
}
