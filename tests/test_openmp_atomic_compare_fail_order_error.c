// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: expected 'seq_cst', 'acquire' or 'relaxed'
int main(void) {
    int x = 0, e = 0, d = 1;
#pragma omp atomic compare fail(release)
    if (x == e) { x = d; }
    return 42;
}
