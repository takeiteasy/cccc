// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'Vec' is a struct template, not a union
@template(T) struct Vec {
    T a;
};
@template(T) @specialize(T *) union Vec {
    T *a;
};
