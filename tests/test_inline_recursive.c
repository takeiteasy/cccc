// JCC_FLAGS: -O2 --inline-limit=100
// Recursive static inline functions fall back to normal CALL
static inline int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main(void) {
    if (fact(0) != 1) return 1;
    if (fact(1) != 1) return 2;
    if (fact(5) != 120) return 3;
    return 42;
}
