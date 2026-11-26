// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: ^int a = 1;
// CCCC_EXPECT_STDOUT: ^int b = 1;
int a = __has_builtin(__builtin_shuffle);
int b = __has_builtin(__builtin_assume);
int main(void) {
    return 42;
}
