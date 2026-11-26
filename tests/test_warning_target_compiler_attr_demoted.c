// CCCC_FLAGS: -E --output-compiler=gcc -Wno-error=target-compiler
// CCCC_EXPECT_STDERR: warning: 'overloadable' is not supported by gcc
// CCCC_EXPECT_STDERR: warning: 'enable_if' is not supported by gcc
// CCCC_EXPECT_STDERR: warning: 'pass_object_size' is not supported by gcc
// CCCC_EXPECT_STDOUT: __attribute__\(\(unavailable\)\) void u\(void\);
// CCCC_EXPECT_STDOUT: struct __attribute__\(\(designated_init\)\) S \{
// CCCC_REJECT_STDOUT: overloadable|enable_if|pass_object_size
__attribute__((overloadable)) int f(int);
int h(int n) __attribute__((enable_if(n > 0, "positive")));
void s(void *const p __attribute__((pass_object_size(0))));
__attribute__((unavailable)) void u(void);
struct __attribute__((designated_init)) S {
    int x;
} S;
int main(void) {
    return 42;
}
