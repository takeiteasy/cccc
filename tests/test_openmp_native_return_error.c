// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: not allowed inside an OpenMP parallel region
int f(void) {
#pragma omp parallel
    { return 1; }
    return 42;
}
int main(void) { return f(); }
