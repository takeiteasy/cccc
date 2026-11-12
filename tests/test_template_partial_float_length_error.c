// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'D' must have an integer type to be an array length
@template(T, int N) struct Arr {
    T v[N];
};
@template(T, double D) @specialize(T[D], 3) struct Arr {
    int q;
};
int main(void) {
    return 42;
}
