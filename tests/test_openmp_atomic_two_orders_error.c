// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: too many memory order clauses
int x, v, c;
int main(void) {
#pragma omp atomic seq_cst relaxed
    x += 1;
    return 42;
}
