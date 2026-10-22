// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: break
int main(void) {
    for (int i = 0; i < 2; i++) {
#pragma omp sections
        {
#pragma omp section
            {
                break;
            }
        }
    }
    return 42;
}
