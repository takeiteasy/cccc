// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: must hold two expression statements
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
