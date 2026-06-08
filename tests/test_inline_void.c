// JCC_FLAGS: -O2
// Void static inline functions with side effects are inlined
static int counter = 0;
static inline void incr(int n) {
    counter += n;
}
int main(void) {
    incr(10);
    incr(20);
    incr(30);
    if (counter != 60) return 1;
    return 42;
}
