// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: takes at least 2 arguments, 1 given
@template(T, U, Ts...) struct A {
    T v;
};
@template(T) @specialize(T *) struct A {
    int q;
};
int main(void) {
    return 42;
}
