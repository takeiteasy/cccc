// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: unknown type name 'Foo'

typedef Foo Bar;
int main(void) {
    return 0;
}
