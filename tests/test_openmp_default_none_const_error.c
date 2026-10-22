// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'c' not specified in enclosing 'parallel'
int main(void) {
    const int c = 3;
    int       r = 0;
#pragma omp parallel default(none) shared(r)
    {
        r = c;
    }
    return 42;
}
