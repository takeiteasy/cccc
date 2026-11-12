// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot expand pack 'Ts' into template 'Pair', which is not variadic
@template(A, B) struct Pair {
    A a;
    B b;
};
@template(T) struct Box {
    T x;
};
@template(T, Ts...) @specialize(Pair @(T, Ts...)) struct Box {
    int q;
};
int main(void) {
    return 42;
}
