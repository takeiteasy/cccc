// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: closely nested inside a loop region
int main(void) {
    int out[4];
#pragma omp parallel for
    for (int i = 0; i < 4; i++) {
#pragma omp ordered
        out[i] = i;
    }
    return 42;
}
