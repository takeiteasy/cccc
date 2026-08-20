#include <stdio.h>
#include <stdlib.h>

#ifndef BENCH_N
#define BENCH_N 200
#endif

static double *a;
static double *b;
static double *c;

static double mrand(unsigned long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return ((double)(*s >> 11)) / (double)(1ULL << 53);
}

int main(void) {
    unsigned long s = 0x9E3779B97F4A7C15ULL;
    long          n = BENCH_N;
    a               = malloc(n * n * sizeof(double));
    b               = malloc(n * n * sizeof(double));
    c               = malloc(n * n * sizeof(double));
    for (long i = 0; i < n * n; i++) {
        a[i] = mrand(&s) - 0.5;
        b[i] = mrand(&s) - 0.5;
    }
    for (long i = 0; i < n; i++) {
        for (long k = 0; k < n; k++) {
            double aik = a[i * n + k];
            for (long j = 0; j < n; j++) {
                c[i * n + j] += aik * b[k * n + j];
            }
        }
    }
    double checksum = 0.0;
    for (long i = 0; i < n * n; i++) {
        checksum += c[i];
    }
    printf("result: %.6f\n", checksum);
    free(a);
    free(b);
    free(c);
    return 42;
}
