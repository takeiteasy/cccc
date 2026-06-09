// CCCC_FLAGS: -O2
// Static inline functions with local variables are inlined
static inline int sum_to(int n) {
    int total = 0;
    for (int i = 1; i <= n; i++)
        total += i;
    return total;
}
static inline int fib(int n) {
    int a = 0, b = 1, tmp;
    for (int i = 0; i < n; i++) {
        tmp = a + b;
        a = b;
        b = tmp;
    }
    return a;
}
int main(void) {
    if (sum_to(5) != 15) return 1;
    if (sum_to(10) != 55) return 2;
    if (fib(0) != 0) return 3;
    if (fib(1) != 1) return 4;
    if (fib(10) != 55) return 5;
    return 42;
}
