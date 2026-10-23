// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'compare' clause is incompatible
int main(void) {
    int x = 0, e = 0, d = 1;
#pragma omp atomic compare write
    x = d;
    return 42;
}
