// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: undefined variable 'Foo'

int main(void) {
    int y = (Foo)-1;
    return 0;
}
