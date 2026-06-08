// Single-return static inline callees expand inline (no CALL emitted)
static inline int add(int a, int b) { return a + b; }
static inline int max(int a, int b) { return a < b ? b : a; }
static inline int clamp(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

int main(void) {
    if (add(10, 20) != 30) return 1;
    if (max(15, 30) != 30) return 2;
    if (add(add(5, 3), 2) != 10) return 3;
    if (clamp(5, 10, 20) != 10) return 4;
    if (clamp(25, 10, 20) != 20) return 5;
    if (clamp(15, 10, 20) != 15) return 6;
    return 42;
}
