// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: needs a default after a defaulted parameter
@template(T = int, U) struct S {
    T a;
};
int main(void) {
    return 42;
}
