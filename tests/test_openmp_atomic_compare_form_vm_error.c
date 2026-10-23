// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: unsupported '#pragma omp atomic compare' statement
int main(void) {
    int x = 0, e = 0, d = 1;
#pragma omp atomic compare
    if (x == e) x = d;
    return 42;
}
