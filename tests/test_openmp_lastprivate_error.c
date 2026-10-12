// Not run natively: OpenMP runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: clause is not allowed here
int main(void) {
    int x = 0;
#pragma omp parallel lastprivate(x)
    { }
    return 42;
}
