// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: ordered\(1\) is smaller than collapse\(2\)
int main(void) {
#pragma omp parallel for collapse(2) ordered(1)
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            ;
    return 42;
}
