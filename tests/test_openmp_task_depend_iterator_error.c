// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: the 'iterator' modifier of 'depend' is not supported
int main(void) {
    int a[4];
#pragma omp task depend(iterator(i = 0 : 4), in : a[i])
    a[0] = 2;
    return 42;
}
