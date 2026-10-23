// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: '#pragma omp atomic write' incompatible with 'acquire' clause
int x, v, c;
int main(void) {
#pragma omp atomic write acquire
    x = v;
    return 42;
}
