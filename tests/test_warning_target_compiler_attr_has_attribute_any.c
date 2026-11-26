// CCCC_FLAGS: -E
// CCCC_EXPECT_STDOUT: ^int a = 1;
// CCCC_EXPECT_STDOUT: ^int b = 1;
// CCCC_EXPECT_STDOUT: ^int c = 1;
int a = __has_attribute(overloadable);
int b = __has_attribute(optimize);
int c = __has_attribute(designated_init);
int main(void) {
    return 42;
}
