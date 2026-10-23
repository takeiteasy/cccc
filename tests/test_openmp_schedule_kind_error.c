// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: unknown schedule kind 'fast'
int main(void) {
    int s = 0;
#pragma omp parallel for schedule(fast) reduction(+ : s)
    for (int i = 0; i < 4; i++)
        s += i;
    return 42;
}
