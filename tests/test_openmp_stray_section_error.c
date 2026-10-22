// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: may only be used in
int main(void) {
#pragma omp section
    {
    }
    return 42;
}
