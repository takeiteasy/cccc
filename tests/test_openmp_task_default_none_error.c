// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'x' not specified in enclosing 'task'
int main(void) {
    int x = 1;
#pragma omp task default(none)
    {
        x = 2;
    }
    return 42;
}
