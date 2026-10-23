// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: a task cannot have its own copy of the multi-dimensional variable-length array 'v'
int main(void) {
    int n = 4, m = 3;
    int v[n][m];
#pragma omp task firstprivate(v)
    v[0][0] = 1;
    return 42;
}
