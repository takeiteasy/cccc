// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: unknown type name 'Foo'
// CCCC_REJECT_STDERR: conflicting|undefined variable
int main(void) {
    int  y = 0;
    Foo *y;
    return 0;
}
