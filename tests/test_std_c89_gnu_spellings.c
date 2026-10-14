// CCCC_FLAGS: --std=c89
// Expected return: 42
// Double-underscore spellings are accepted even under strict c89.
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
    return twice(sum(&a, &b)) + tls;
}
