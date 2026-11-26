// CCCC_FLAGS: -E --output-compiler=clang
// CCCC_EXPECT_STDOUT: ^int a = 0;
// CCCC_EXPECT_STDOUT: ^int b = 1;
int a = __has_builtin(__builtin_shuffle);
int b = __has_builtin(__builtin_assume);
int main(void) {
    return 42;
}
