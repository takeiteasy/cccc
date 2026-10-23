// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: closely nested inside a loop region
int main(void) {
    int out[4];
#pragma omp parallel
    {
#pragma omp ordered
        out[0] = 1;
    }
    return 42;
}
