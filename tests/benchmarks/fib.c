#include <stdio.h>

#ifndef BENCH_N
#define BENCH_N 30
#endif

static long fib(long n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main(void) {
    long r = fib(BENCH_N);
    printf("result: %ld\n", r);
    return 42;
}
