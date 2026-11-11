// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: in instantiation of template 'Vec' \(Vec__int\)
@template(T) struct Vec {
    T a;
};
@specialize(int) struct Vec {
    T a;
};
@inst(int) Vec v;
