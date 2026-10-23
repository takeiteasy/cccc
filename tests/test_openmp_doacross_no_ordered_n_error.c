// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: closely nested inside a loop region with an 'ordered\(n\)'
int main(void) {
    int a[8] = {0};
#pragma omp parallel for ordered
    for (int i = 1; i < 8; i++) {
#pragma omp ordered depend(sink : i - 1)
        a[i] = a[i - 1];
    }
    return 42;
}
