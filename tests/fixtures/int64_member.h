#include <stdint.h>
#include <stdio.h>

struct Int64Pair {
    int64_t  a;
    uint64_t b;
};

struct Int64Pair int64_pair_make(int64_t a, uint64_t b);
