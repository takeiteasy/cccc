// CCCC_NATIVE_SKIP: OpenMP runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: does not apply to
struct S { int a; };
int main(void) {
    struct S s = {0};
#pragma omp parallel reduction(+ : s)
    { }
    return 42;
}
