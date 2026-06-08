// JCC_FLAGS: -O2
// Multi-statement static inline functions expand inline
static inline int clamp(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
static inline int min(int a, int b) {
    if (a < b) return a;
    return b;
}
static inline int max(int a, int b) {
    if (a > b) return a;
    return b;
}
static inline int sign(int x) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    return 0;
}
int main(void) {
    if (clamp(5, 10, 20) != 10) return 1;
    if (clamp(25, 10, 20) != 20) return 2;
    if (clamp(15, 10, 20) != 15) return 3;
    if (min(3, 7) != 3) return 4;
    if (max(3, 7) != 7) return 5;
    if (sign(42) != 1) return 6;
    if (sign(-5) != -1) return 7;
    if (sign(0) != 0) return 8;
    if (clamp(min(0, 50), 10, 30) != 10) return 9;
    if (clamp(max(0, 50), 10, 30) != 30) return 10;
    if (min(clamp(-5, 0, 10), 5) != 0) return 11;
    return 42;
}
