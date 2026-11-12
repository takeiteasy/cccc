// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of partial specialization of 'A'
@template(Ts...) struct A {
    Ts... v;
};
@template(T, Ts...) @specialize(T, Ts...) struct A {
    int q;
};
@template(U, Us...) @specialize(U, Us...) struct A {
    int r;
};
int main(void) {
    return 42;
}
