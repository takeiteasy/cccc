// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: unsupported '#pragma omp atomic' statement
int x, v, c;
int main(void) {
#pragma omp atomic 
    x = x * 2 + 1;
    return 42;
}
