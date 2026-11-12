// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pattern over a template instance is not supported
@template(T) struct Vec {
    T a;
};
@template(T) struct Pair {
    T a;
};
@template(T) @specialize(@inst(T) Pair) struct Vec {
    T b;
};
