// EXPECT_COMPILE_ERROR
// CCCC_NATIVE_SKIP: the builtin is supported under -c=native
// CCCC_EXPECT_STDERR: '__builtin_os_log_format_buffer_size' is supported only when cccc emits C
//
// The os_log encoder is implemented by the host compiler, so the VM rejects a call.
int main(void) {
    return (int)__builtin_os_log_format_buffer_size("hi %d", 3);
}
