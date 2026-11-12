// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter pack cannot have a default
@template(Ts... = int) struct A { int x; };
int main(void) {
    return 42;
}
