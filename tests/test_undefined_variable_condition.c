// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: undefined variable 'Foo'

int main(void) {
    if (Foo) {
    }
    return 0;
}
