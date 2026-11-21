// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: unknown type name 'Foo'
// CCCC_REJECT_STDERR: expected
int main(void) {
    for (Foo i = 0; i < 3; i++) {
    }
    return 0;
}
