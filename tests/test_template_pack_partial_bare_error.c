// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: parameter pack 'Ts' must be expanded with
@template(T, Ts...) struct A {
    T v;
};
@template(T, Ts...) @specialize(T, Ts) struct A {
    int q;
};
int main(void) {
    return 42;
}
