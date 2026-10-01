// CCCC_FLAGS: -fopenmp -m -o /dev/stdout
// CCCC_EXPECT_STDOUT: __omp_region_
// CCCC_REJECT_STDOUT: #pragma omp
//
// -m lowers the region and does not also replay the directive at file scope.
#include <omp.h>

int main(void) {
    int n = 0;
#pragma omp parallel for reduction(+ : n)
    for (int i = 0; i < 4; i++)
        n += 1;
    return n == 4 ? 42 : 1;
}
