// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: must be an integer
int main(void) {
    int a[4];
#pragma omp parallel for
    for (int *p = a; p < a + 4; p++)
        *p = 1;
    return 42;
}
