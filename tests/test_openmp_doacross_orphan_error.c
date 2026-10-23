// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: closely nested inside a loop region with an 'ordered\(n\)'
void f(int *a, int i) {
#pragma omp ordered depend(source)
    a[i] = 1;
}
int main(void) {
    return 42;
}
