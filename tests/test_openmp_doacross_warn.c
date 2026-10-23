// CCCC_FLAGS: -fopenmp
// CCCC_EXPECT_STDERR: waits for the current or a later iteration; ignored
// CCCC_EXPECT_STDERR: refers to an iteration the loop never runs; ignored
// The dropped sinks do not wait, so the loop finishes.
int main(void) {
    long a[40] = {0};
#pragma omp parallel for ordered(1) num_threads(4) schedule(static, 1)
    for (int i = 0; i < 20; i += 2) {
#pragma omp ordered depend(sink : i)
#pragma omp ordered depend(sink : i + 2)
#pragma omp ordered depend(sink : i - 1)
        a[i] = i;
#pragma omp ordered depend(source)
    }
    long sum = 0;
    for (int i = 0; i < 20; i += 2)
        sum += a[i];
    return sum == 90 ? 42 : 1;
}
