// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: ^int a = 1;
// CCCC_EXPECT_STDOUT: ^int b = 1;
// CCCC_EXPECT_STDOUT: ^int c = 1;
// CCCC_EXPECT_STDOUT: ^int d = 1;
// CCCC_EXPECT_STDOUT: ^int e = 0;
int a = __has_builtin(__builtin_nans);
int b = __has_builtin(__builtin_atomic_thread_fence);
int c = __has_builtin(__c11_atomic_fetch_add);
int d = __has_builtin(__builtin_setjmp);
int e = __has_builtin(__builtin_no_such_builtin);
int main(void) {
    return 42;
}
