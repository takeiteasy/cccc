#include <stdio.h>
#include <string.h>

#ifndef BENCH_LIMIT
#define BENCH_LIMIT 10000000
#endif

int main(void) {
    static unsigned char sieve[BENCH_LIMIT];
    memset(sieve, 1, sizeof(sieve));
    sieve[0] = sieve[1] = 0;
    for (long i = 2; (long)i * (long)i < (long)BENCH_LIMIT; i++) {
        if (sieve[i]) {
            for (long j = i * i; j < (long)BENCH_LIMIT; j += i) {
                sieve[j] = 0;
            }
        }
    }
    long count = 0;
    long checksum = 0;
    for (long i = 0; i < (long)BENCH_LIMIT; i++) {
        if (sieve[i]) {
            count++;
            checksum += i;
        }
    }
    printf("result: count=%ld checksum=%ld\n", count, checksum);
    return 42;
}
