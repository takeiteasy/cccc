// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: element 1 of the .sink. vector must be .i.
int main(void) {
    int a[8] = {0};
#pragma omp parallel for ordered(1)
    for (int i = 1; i < 8; i++) {
#pragma omp ordered depend(sink : j - 1)
        a[i] = 1;
#pragma omp ordered depend(source)
    }
    return 42;
}
