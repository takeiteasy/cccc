// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: appears twice in the pattern
@template(int A, int B) struct Grid {
    char c[A * B];
};
@template(int N) @specialize(N, N) struct Grid {
    char c[N];
};
