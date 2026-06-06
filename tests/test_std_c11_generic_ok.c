// JCC_FLAGS: --std=c11
#define myabs(x) _Generic((x), int: (x) < 0 ? -(x) : (x), double: (x) < 0.0 ? -(x) : (x))
int main(void) { return myabs(-5) == 5 ? 42 : 1; }
