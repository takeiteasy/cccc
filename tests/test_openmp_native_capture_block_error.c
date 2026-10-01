// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: capture blocks are not supported
int main(void) {
    int x = 0, v;
#pragma omp parallel
    {
#pragma omp atomic capture
        { v = x; x += 1; }
    }
    return 42;
}
