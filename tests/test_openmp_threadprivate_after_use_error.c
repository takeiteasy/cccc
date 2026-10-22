// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: declared .threadprivate. after first use
int x;
int f(void) {
    return x;
}
#pragma omp threadprivate(x)
int main(void) {
    return f();
}
