// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of partial specialization of 'Pair'
@template(int X, int Y) struct Pair {
    char kind;
};
@template(int N) @specialize(N, N) struct Pair {
    char a;
};
@template(signed int M) @specialize(M, M) struct Pair {
    char b;
};
int main(void) {
    return 42;
}
