// CCCC_FLAGS: -E --output-compiler=clang -Wno-error=target-compiler
// CCCC_EXPECT_STDERR: warning: nested functions are not supported by clang
// CCCC_EXPECT_STDERR: warning: '__builtin_shuffle' is not supported by clang
// CCCC_EXPECT_STDOUT: int g\(int y\)
// CCCC_EXPECT_STDOUT: __builtin_shuffle
typedef int v4 __attribute__((vector_size(16)));
v4 s(v4 a, v4 m) {
    return __builtin_shuffle(a, m);
}
int f(int x) {
    int g(int y) {
        return x + y;
    }
    return g(1);
}
int main(void) {
    return 42;
}
