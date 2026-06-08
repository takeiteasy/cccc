// JCC_FLAGS: -O2
// Functions above the inline limit (default 20) fall back to normal CALL.
// This function has ~25 AST nodes, exceeding the default threshold.
static inline int big_add(int a, int b, int c, int d) {
    int r = a;
    r += b;
    r += c;
    r += d;
    return r;
}
int main(void) {
    if (big_add(1, 2, 3, 4) != 10) return 1;
    if (big_add(10, 20, 30, 40) != 100) return 2;
    return 42;
}
