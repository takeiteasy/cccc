// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pack 'Ts' appears twice in the pattern
@template(Ts...) struct P {
    Ts... v;
};
@template(Ts...) struct A {
    Ts... v;
};
@template(Ts...) @specialize(P @(Ts...), Ts...) struct A {
    int q;
};
int main(void) {
    return 42;
}
