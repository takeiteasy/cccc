// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a pack expansion cannot be nested in another
@template(Ts...) struct Pack {
    Ts... m;
};
@template(Ts...) struct A {
    Ts... v;
};
@template(Ts...) @specialize(Pack @(Ts...)...) struct A {
    int q;
};
int main(void) {
    return 42;
}
