// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: 'depend\(depobj: \.\.\.\)' is not supported
int main(void) {
    int x = 1;
#pragma omp task depend(depobj : x)
    x = 2;
    return 42;
}
