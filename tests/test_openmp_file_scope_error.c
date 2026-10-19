// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: file scope
#pragma omp parallel
int x;
int main(void) { return 42; }
