// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: invalid sync hint value
int x, v, c;
int main(void) {
#pragma omp atomic hint(3)
    x += 1;
    return 42;
}
