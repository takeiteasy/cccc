// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'fail' clause requires 'compare' clause
int main(void) {
    int x = 0, e = 0, d = 1;
#pragma omp atomic fail(relaxed)
    x++;
    return 42;
}
