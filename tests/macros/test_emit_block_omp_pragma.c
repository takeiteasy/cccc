// CCCC_NATIVE_SKIP: OpenMP runs in the VM only
// CCCC_FLAGS: -fopenmp
// A #pragma omp inside a function body in a `#pragma cccc emit` block is a
// statement: it must be spliced to `__cccc_omp(...)` as in ordinary source,
// not hoisted as a file-scope emit marker (which left a
// __builtin_emit_line__ call inside the function body).

#pragma cccc comptime begin
#pragma cccc emit begin
int emitted_omp_sum(void) {
    int sum = 0;
#pragma omp parallel for reduction(+ : sum)
    for (int i = 0; i < 4; i++)
        sum += i;
    return sum;
}
#pragma cccc emit end
#pragma cccc comptime end

int main(void) {
    return emitted_omp_sum() == 6 ? 42 : 1;
}
