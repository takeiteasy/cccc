// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: undefined variable 'Foo'

int main(void) {
    int y = sizeof(Foo);
    return 0;
}
