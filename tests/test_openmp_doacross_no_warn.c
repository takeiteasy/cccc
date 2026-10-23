// CCCC_FLAGS: -fopenmp -Wno-openmp
// CCCC_REJECT_STDERR: sink
int main(void) {
    long a[8] = {0};
#pragma omp parallel for ordered(1) num_threads(2)
    for (int i = 0; i < 8; i++) {
#pragma omp ordered depend(sink : i)
        a[i] = i;
#pragma omp ordered depend(source)
    }
    return a[7] == 7 ? 42 : 1;
}
