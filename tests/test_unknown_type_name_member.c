// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: unknown type name 'Foo'

struct S {
    Foo m;
};
int main(void) {
    return 0;
}
