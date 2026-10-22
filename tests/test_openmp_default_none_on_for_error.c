// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'default' clause is not allowed here
int main(void) {
#pragma omp parallel
    {
#pragma omp for default(none)
        for (int i = 0; i < 4; i++)
            ;
    }
    return 42;
}
