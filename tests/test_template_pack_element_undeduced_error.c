// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: partial specialization parameter 'T' is not deduced by the pattern
@template(T, U) struct Link {
    T a;
    U b;
};
@template(Ts...) struct A {
    Ts... v;
};
@template(T, Ts...) @specialize(Link @(T, Ts)...) struct A {
    int q;
};
int main(void) {
    return 42;
}
