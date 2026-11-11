// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: specialization of 'Vec__int' after its first use
@template(T) struct Vec {
    T a;
};
@inst(int) Vec v;
@specialize(int) struct Vec {
    int a, b;
};
