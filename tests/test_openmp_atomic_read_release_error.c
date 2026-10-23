// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: '#pragma omp atomic read' incompatible with 'release' clause
int x, v, c;
int main(void) {
#pragma omp atomic read release
    v = x;
    return 42;
}
