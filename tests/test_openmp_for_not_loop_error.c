// CCCC_NATIVE_SKIP: OpenMP runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: must be a for loop
int main(void) {
#pragma omp for
    while (0) { }
    return 42;
}
