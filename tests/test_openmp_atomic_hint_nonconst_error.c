// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: expected a constant integer expression in 'hint' clause
int x, v, c;
int main(void) {
#pragma omp atomic hint(c)
    x += 1;
    return 42;
}
