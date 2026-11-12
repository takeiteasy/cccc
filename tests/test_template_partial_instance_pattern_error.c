// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot name an instance of typedef template 'Ptr'
@template(T) struct Vec {
    T a;
};
@template(T) typedef T *Ptr;
@template(T) @specialize(Ptr @(T)) struct Vec {
    T b;
};
