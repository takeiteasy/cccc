// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: is not a template
@template(T) @specialize(T *) struct Nope {
    T *a;
};
