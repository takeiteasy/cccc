// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter pack 'Ts' must be expanded with '...'
@template(Ts...) struct A {
    Ts... v;
};
@template(Ts...) @specialize(Ts *, Ts...) struct A {
    int q;
};
int main(void) {
    return 42;
}
