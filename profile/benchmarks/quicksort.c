#include <stdio.h>
#include <stdlib.h>

#ifndef BENCH_N
#define BENCH_N 100000
#endif

static int *arr;

static void swap(int *x, int *y) {
    int t = *x;
    *x = *y;
    *y = t;
}

static int partition(int lo, int hi) {
    int pivot = arr[hi];
    int i = lo - 1;
    for (int j = lo; j < hi; j++) {
        if (arr[j] < pivot) {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[hi]);
    return i + 1;
}

static void qs(int lo, int hi) {
    if (lo < hi) {
        int p = partition(lo, hi);
        qs(lo, p - 1);
        qs(p + 1, hi);
    }
}

static unsigned long mrand(unsigned long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 11;
}

int main(void) {
    unsigned long s = 0x9E3779B97F4A7C15ULL;
    long n = BENCH_N;
    arr = malloc(n * sizeof(int));
    for (long i = 0; i < n; i++) {
        arr[i] = (int)(mrand(&s) & 0x7FFFFFFFL);
    }
    qs(0, (int)n - 1);
    long checksum = 0;
    for (long i = 0; i < n; i++) {
        checksum += arr[i];
    }
    printf("result: %ld\n", checksum);
    free(arr);
    return 42;
}
