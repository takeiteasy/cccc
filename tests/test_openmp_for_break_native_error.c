// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp -c=native
// CCCC_EXPECT_STDERR: break statement used with OpenMP for loop
int main(void) {
#pragma omp parallel for
    for (int i = 0; i < 8; i++) {
        if (i == 3)
            break;
    }
    return 42;
}
