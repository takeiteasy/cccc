// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: integer or a pointer to an object type
int main(void) {
    char a[4];
#pragma omp parallel for
    for (void *p = a; p < (void *)(a + 4); p = (char *)p + 1)
        ;
    return 42;
}
