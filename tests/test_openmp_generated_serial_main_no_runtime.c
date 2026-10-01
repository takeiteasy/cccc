// CCCC_FLAGS: -fopenmp -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: int answer\(void\)
// CCCC_REJECT_STDOUT: __cccc_pool_run|__cccc_omp_|pragma omp|omp\.h
//
// A region in hand-written code is not emitted by -c=generated, so it needs
// no OpenMP runtime there.
#include <omp.h>

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("answer", GetType("int"));
    FunctionSetBody(fn, Quote("return 42;"));
}
gen();

int main(void) {
    int n = 0;
#pragma omp parallel for reduction(+ : n)
    for (int i = 0; i < 4; i++)
        n += 1;
    return n == 4 ? answer() : 1;
}
