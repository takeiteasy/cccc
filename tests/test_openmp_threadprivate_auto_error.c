// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: must be a global or static variable
int main(void) {
    int x = 0;
#pragma omp threadprivate(x)
    return 42;
}
