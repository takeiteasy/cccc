// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: ambiguous partial specializations of 'A'
@template(Ts...) struct A {
    Ts... v;
};
@template(T, Ts...) @specialize(int, T, Ts...) struct A {
    int q;
};
@template(T, Ts...) @specialize(T, int, Ts...) struct A {
    int r;
};
A @(int, int) a;
int main(void) {
    return 42;
}
