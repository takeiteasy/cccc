// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: unknown type name 'Foo'
// CCCC_REJECT_STDERR: expected
int main(void) {
    Foo y;
    return 0;
}
