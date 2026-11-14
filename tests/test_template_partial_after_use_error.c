// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: partial specialization of 'Vec' after 'Vec_int_ptr' was instantiated
@template(T) struct Vec {
    T a;
};
@inst(int *) Vec v;
@template(T) @specialize(T *) struct Vec {
    T *a;
};
