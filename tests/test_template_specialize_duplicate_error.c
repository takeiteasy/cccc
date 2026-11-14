// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of specialization 'Vec_int'
@template(T) struct Vec {
    T a;
};
@specialize(int) struct Vec {
    int a, b;
};
@specialize(int) struct Vec {
    int c;
};
