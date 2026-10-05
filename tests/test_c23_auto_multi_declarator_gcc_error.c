// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'auto' may only be used with a single declarator
int main(void) {
    auto x = 1, y = 2;
    return x + y;
}
