// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: takes at least 1 argument, 0 given
@template(T, Ts...) struct A {
    T v;
};
@template(Ts...) @specialize(Ts...) struct A {
    int q;
};
int main(void) {
    return 42;
}
