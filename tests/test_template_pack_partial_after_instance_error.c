// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: partial specialization of 'A' after 'A__int__char' was instantiated
@template(Ts...) struct A {
    Ts... v;
};
A @(int, char) a;
@template(T, Ts...) @specialize(T, Ts...) struct A {
    int q;
};
int main(void) {
    return 42;
}
