// CCCC_FLAGS: -Wattributes -Wsentinel
// CCCC_EXPECT_STDERR: sentinel attribute only applies to variadic functions
// CCCC_REJECT_STDERR: not enough variable arguments
// C23 attribute syntax variant of test_warning_sentinel_non_variadic.c (#696).
[[gnu::sentinel]] void foo(int a);
void foo(int a) { }
int main(void) {
    foo(1);
    return 42;
}
