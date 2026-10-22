// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'sum' not specified in enclosing 'parallel'
int main(void) {
    int sum = 0;
#pragma omp parallel default(none)
    {
#pragma omp for reduction(+ : sum)
        for (int i = 0; i < 4; i++)
            sum += i;
    }
    return 42;
}
