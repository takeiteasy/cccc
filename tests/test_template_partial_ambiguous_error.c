// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: ambiguous partial specializations of 'Two'
@template(A, B) struct Two {
    A a;
    B b;
};
@template(T, U) @specialize(T *, U) struct Two {
    T *a;
};
@template(T, U) @specialize(U, T *) struct Two {
    T *b;
};
@inst(int *, int *) Two t;
