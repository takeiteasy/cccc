// CCCC_FLAGS: -E --output-compiler=clang -Wall
// CCCC_REJECT_STDERR: (warning|error):
// CCCC_EXPECT_STDOUT: twice
#include <stdio.h>
#include <stdlib.h>
@template(T) T twice(T x) {
    return x + x;
}
int main(void) {
    int unused;
    int s = @inst(int) twice(2);
#pragma omp parallel for reduction(+ : s)
    for (int i = 0; i < 4; i++)
        s += i;
    return 42;
}
