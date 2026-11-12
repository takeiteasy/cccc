// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot have a default
@template(T) struct Vec {
    T a;
};
@template(T, int N = 1) @specialize(T *) struct Vec {
    T *a;
};
