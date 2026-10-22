// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'x' in 'copyin' clause is not threadprivate
int main(void) {
    int x = 0;
#pragma omp parallel copyin(x)
    { }
    return 42;
}
