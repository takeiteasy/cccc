// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: is not deduced by the pattern
@template(T) struct Vec {
    T a;
};
@template(T, U) @specialize(T *) struct Vec {
    T *a;
    U  b;
};
