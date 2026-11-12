// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must be more specific than the primary template
@template(T) struct Vec {
    T a;
};
@template(T) @specialize(T) struct Vec {
    T b;
};
