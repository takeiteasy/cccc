// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: ambiguous partial specializations of 'Pair'
@template(long X, long Y) struct Pair {
    char kind;
};
@template(int N) @specialize(N, N) struct Pair {
    char a;
};
@template(unsigned M) @specialize(M, M) struct Pair {
    char b;
};
int main(void) {
    @inst(1, 1) Pair p;
    return 42;
}
