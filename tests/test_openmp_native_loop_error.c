// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: canonical form
int main(void) {
    int n = 8;
#pragma omp parallel for
    for (int i = 0; i * 2 < n; i++)
        ;
    return 42;
}
