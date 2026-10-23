// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: unsupported '#pragma omp atomic capture' block
int main(void) {
    int x = 0, v, w;
#pragma omp parallel
    {
#pragma omp atomic capture
        {
            v = x;
            w = x;
        }
    }
    return 42;
}
