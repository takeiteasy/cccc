// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: its default names an earlier parameter
@template(T, int N = sizeof(T)) struct Arr {
    T v[N];
};
@template(T) @specialize(T *) struct Arr {
    T *v;
};
