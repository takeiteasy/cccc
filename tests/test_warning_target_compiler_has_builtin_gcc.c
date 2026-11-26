// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDOUT: ^int a = 1;
// CCCC_EXPECT_STDOUT: ^int b = 0;
// CCCC_EXPECT_STDOUT: ^int c = 1;
int a = __has_builtin(__builtin_shuffle);
int b = __has_builtin(__builtin_assume);
int c = __has_builtin(__builtin_expect);
int main(void) {
    return 42;
}
