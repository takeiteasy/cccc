// Without -fopenmp a #pragma omp is ignored silently and _OPENMP is undefined.
// CCCC_FLAGS: -Werror -Wall
#ifdef _OPENMP
#error _OPENMP must be undefined without -fopenmp
#endif
int main(void) {
    int sum = 0;
#pragma omp parallel for reduction(+ : sum)
    for (int i = 0; i < 4; i++)
        sum += i;
    return sum == 6 ? 42 : 1;
}
