// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter pack must be the last parameter
@template(Ts..., Us...) struct A { int x; };
int main(void) {
    return 42;
}
