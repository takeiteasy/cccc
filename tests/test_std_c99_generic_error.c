// EXPECT_COMPILE_ERROR
// JCC_FLAGS: -std=c99
// JCC_EXPECT_STDERR: '_Generic' is not available before C11
#define abs(x) _Generic((x), int: (x) < 0 ? -(x) : (x))
int main(void) { return abs(-5) == 5 ? 0 : 1; }
