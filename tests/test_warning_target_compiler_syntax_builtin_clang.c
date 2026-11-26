// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=clang
// CCCC_EXPECT_STDERR: error: '__builtin_shuffle' is not supported by clang \[-Wtarget-compiler\]
typedef int v4 __attribute__((vector_size(16)));
v4 s(v4 a, v4 m) {
    return __builtin_shuffle(a, m);
}
int main(void) {
    return 42;
}
