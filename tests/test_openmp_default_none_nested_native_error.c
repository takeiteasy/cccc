// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: 'x' not specified in enclosing 'parallel'
int main(void) {
    int x = 1, r = 0;
#pragma omp parallel default(none) shared(r)
    {
#pragma omp parallel shared(r)
        {
            r = x;
        }
    }
    return 42;
}
