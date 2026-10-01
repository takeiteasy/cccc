// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: must be rectangular
int main(void) {
    int s = 0;
#pragma omp parallel for collapse(2) reduction(+ : s)
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < i; j++)
            s++;
    return 42;
}
