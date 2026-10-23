// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: schedule\(runtime\) takes no chunk size
int main(void) {
    int s = 0;
#pragma omp parallel for schedule(runtime, 2) reduction(+ : s)
    for (int i = 0; i < 4; i++)
        s += i;
    return 42;
}
