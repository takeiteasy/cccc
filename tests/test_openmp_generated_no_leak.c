// CCCC_FLAGS: -fopenmp -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: __omp_region_[\s\S]*__cccc_pool_run
// CCCC_REJECT_STDOUT: #pragma omp|omp\.h|__OMP_H
//
// -c=generated lowers a Quote()-authored OpenMP region onto the thread pool
// and emits the runtime. It replays neither the hand-written `#pragma omp`
// nor the cccc-only omp.h include and its guard.
#include <omp.h>

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("psum", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(
            fn, Quote("int sum = 0;"
                      "_Pragma(\"omp parallel for reduction(+:sum)\")"
                      "for (int i = 0; i < 10; i++) sum += i;"
                      "return sum;"));
    }
}
gen();

int main(void) {
    int n = 0;
#pragma omp parallel for reduction(+ : n)
    for (int i = 0; i < 4; i++)
        n += 1;
    return psum() == 45 && n == 4 ? 42 : 1;
}
