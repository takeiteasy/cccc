// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'g' not specified in enclosing 'parallel'
int g;
int main(void) {
#pragma omp parallel default(none)
    {
        g = 1;
    }
    return 42;
}
