// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: unsupported OpenMP directive
int main(void) {
#pragma omp task
    { }
    return 42;
}
