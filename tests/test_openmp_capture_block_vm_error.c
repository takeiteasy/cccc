// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: unsupported .#pragma omp atomic capture. block
int main(void) {
    int x = 0, v;
#pragma omp parallel
    {
#pragma omp atomic capture
        {
            v  = x;
            x += 1;
            x += 1;
        }
    }
    return 42;
}
