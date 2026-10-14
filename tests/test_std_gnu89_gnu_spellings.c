// CCCC_FLAGS: --std=gnu89
// Expected return: 42
// GNU spellings and plain `inline` are accepted under gnu89.
static inline int plain(int x) {
    return x;
}

static __inline int twice(int x) {
    return 2 * x;
}

static __inline__ int sum(int *__restrict a, int *__restrict__ b) {
    return *a + *b;
}

__thread int tls;

int main(void) {
    int a = 10, b = 11;
    tls = 0;
    return plain(twice(sum(&a, &b))) + tls;
}
