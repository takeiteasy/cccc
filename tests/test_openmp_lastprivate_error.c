// CCCC_NATIVE_SKIP: OpenMP runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: unsupported OpenMP clause
int main(void) {
    int x = 0;
#pragma omp parallel lastprivate(x)
    { }
    return 42;
}
