// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: 'weak' clause requires atomic equality comparison
int main(void) {
    int x = 0, e = 0, d = 1;
#pragma omp atomic compare weak
    if (x < e) { x = e; }
    return 42;
}
