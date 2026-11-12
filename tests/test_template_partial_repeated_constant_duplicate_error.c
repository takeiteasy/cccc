// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of partial specialization of 'Grid'
@template(int A, int B) struct Grid {
    char c[A * B];
};
@template(int N) @specialize(N, N) struct Grid {
    char c[N];
};
@template(int M) @specialize(M, M) struct Grid {
    char c[M];
};
