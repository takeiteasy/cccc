// CCCC_NATIVE_SKIP: OpenMP runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: cannot be combined with 'nowait'
int main(void) {
    int x = 0;
#pragma omp parallel private(x)
    {
#pragma omp single copyprivate(x) nowait
        { x = 1; }
    }
    return 42;
}
