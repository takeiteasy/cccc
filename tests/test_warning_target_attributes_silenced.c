// CCCC_FLAGS: -E --output-compiler=clang -Wno-target-attributes
// CCCC_REJECT_STDERR: target-attributes|warning|error
// CCCC_EXPECT_STDOUT: __attribute__\(\(cold\)\) void k\(void\);
// CCCC_EXPECT_STDOUT: __attribute__\(\(overloadable\)\) int f\(int\);
// CCCC_REJECT_STDOUT: optimize|designated_init
__attribute__((optimize("O2"), cold)) void k(void);
__attribute__((overloadable)) int f(int);
struct __attribute__((designated_init)) S {
    int x;
};
int main(void) {
    return 42;
}
