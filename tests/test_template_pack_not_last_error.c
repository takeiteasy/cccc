// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter pack must be the last parameter
@template(Ts..., T) struct A { T x; };
int main(void) {
    return 42;
}
