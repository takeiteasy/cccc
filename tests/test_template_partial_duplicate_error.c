// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of partial specialization of 'Vec'
@template(T) struct Vec {
    T a;
};
@template(T) @specialize(T *) struct Vec {
    T *a;
};
@template(U) @specialize(U *) struct Vec {
    U *b;
};
