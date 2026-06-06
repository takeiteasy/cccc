// JCC_FLAGS: --std=c99
static inline int add(int a, int b) { return a + b; }
int main(void) { return add(1, 2) == 3 ? 42 : 1; }
