// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: cannot be used in a loop with .ordered\(n\).
int main(void) {
    int a[8] = {0};
#pragma omp parallel for ordered(1)
    for (int i = 1; i < 8; i++) {
#pragma omp ordered
        a[i] = 1;
    }
    return 42;
}
