// CCCC_FLAGS: -Wattributes
// CCCC_EXPECT_STDERR: warning: unknown attribute 'noinline' ignored
int __attribute__((noinline)) x;
int main(void) {
    return 42;
}
