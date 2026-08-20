#include <stdio.h>

#ifndef BENCH_M
#define BENCH_M 3
#endif
#ifndef BENCH_N
#define BENCH_N 8
#endif

static long ack(long m, long n) {
    if (m == 0)
        return n + 1;
    if (n == 0)
        return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}

int main(void) {
    long r = ack(BENCH_M, BENCH_N);
    printf("result: %ld\n", r);
    return 42;
}
