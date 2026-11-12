// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: partial specialization parameter 'Ts' is not deduced by the pattern
@template(Ts...) struct A {
    Ts... v;
};
@template(T, Ts...) @specialize(T *) struct A {
    int q;
};
int main(void) {
    return 42;
}
