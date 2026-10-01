// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: not supported with -c=native
int main(void) {
#pragma omp parallel
    { }
    return 42;
}
