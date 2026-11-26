// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDOUT: ^int a = 0;
// CCCC_EXPECT_STDOUT: ^int b = 0;
// CCCC_EXPECT_STDOUT: ^int c = 1;
int a = __has_builtin(__builtin_pc_function_name);
int b = __has_builtin(__c11_atomic_fetch_add);
int c = __has_builtin(__atomic_fetch_add);
int main(void) {
    return 42;
}
