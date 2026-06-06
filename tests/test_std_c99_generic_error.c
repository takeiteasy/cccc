// JCC_FLAGS: --std=c99 -Wpedantic
// JCC_EXPECT_STDERR: warning: '_Generic' is a C11 extension \[-Wpedantic\]
#define abs(x) _Generic((x), int: (x) < 0 ? -(x) : (x))
int main(void) { return abs(-5) == 5 ? 42 : 1; }
