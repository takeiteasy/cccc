// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: must be an integer or a pointer
int main(void) {
    double d = 0;
#pragma omp parallel for linear(d)
    for (int i = 0; i < 4; i++)
        ;
    return 42;
}
