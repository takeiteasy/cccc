// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: a task cannot have its own copy of the variable-length array 'v'
int main(void) {
    int n = 4;
    int v[n];
#pragma omp task firstprivate(v)
    v[0] = 1;
    return 42;
}
