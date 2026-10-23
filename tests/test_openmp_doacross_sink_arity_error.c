// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: sink. vector has 1 element\(s\), but the loop has ordered\(2\)
int main(void) {
    int a[8][8] = {{0}};
#pragma omp parallel for ordered(2)
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
#pragma omp ordered depend(sink : i - 1)
            a[i][j] = 1;
#pragma omp ordered depend(source)
        }
    return 42;
}
