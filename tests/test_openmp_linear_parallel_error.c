// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'linear' clause is not allowed here
int main(void) {
    int x = 0;
#pragma omp parallel linear(x)
    {
    }
    return 42;
}
