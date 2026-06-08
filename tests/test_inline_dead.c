// Unused static inline functions produce no bytecode
static inline int unused(void) { return 42; }
static inline int also_unused(int a, int b) { return a + b; }
int main(void) { return 42; }
