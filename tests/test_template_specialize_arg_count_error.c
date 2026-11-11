// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: template 'Pair' takes 2 arguments, 1 given
@template(A, B) struct Pair {
    A a;
    B b;
};
@specialize(int) struct Pair {
    int a;
};
