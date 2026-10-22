// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'n' not specified in enclosing 'parallel'
int main(void) {
    int n = 4, s = 0;
#pragma omp parallel for default(none) shared(s)
    for (int i = 0; i < n; i++)
        s += i;
    return 42;
}
