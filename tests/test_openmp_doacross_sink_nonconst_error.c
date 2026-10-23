// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
int main(void) {
    int a[8] = {0}, n = 1;
#pragma omp parallel for ordered(1)
    for (int i = 1; i < 8; i++) {
#pragma omp ordered depend(sink : i - n)
        a[i] = 1;
#pragma omp ordered depend(source)
    }
    return 42;
}
