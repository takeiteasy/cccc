// Test #268: restrict memcpy loop lowering.
// for (size_t i = 0; i < n; i++) dst[i] = src[i]
// with restrict dst and src must be lowered to MCPY.

#include <string.h>

static int passed = 0;

// Basic byte copy
static void byte_copy(char *restrict dst, const char *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_byte_copy(void) {
    char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    char dst[8] = {0};
    byte_copy(dst, src, 8);
    for (int i = 0; i < 8; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    passed++;
}

// Int copy
static void int_copy(int *restrict dst, const int *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_int_copy(void) {
    int src[5] = {10, 20, 30, 40, 50};
    int dst[5] = {0};
    int_copy(dst, src, 5);
    for (int i = 0; i < 5; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    passed++;
}

// Long copy
static void long_copy(long *restrict dst, const long *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_long_copy(void) {
    long src[4] = {100, 200, 300, 400};
    long dst[4] = {0};
    long_copy(dst, src, 4);
    for (int i = 0; i < 4; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    passed++;
}

// Non-restrict loop must still work correctly (uses the regular loop path)
static void non_restrict_copy(int *dst, const int *src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_non_restrict_copy(void) {
    int src[3] = {7, 8, 9};
    int dst[3] = {0};
    non_restrict_copy(dst, src, 3);
    for (int i = 0; i < 3; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    passed++;
}

// Copy of zero elements must be a no-op
static void test_zero_len(void) {
    int src[2] = {1, 2};
    int dst[2] = {99, 99};
    int_copy(dst, src, 0);
    if (dst[0] != 99 || dst[1] != 99)
        __builtin_trap();
    passed++;
}

// size_t induction variable
static void sizet_copy(long *restrict dst, const long *restrict src, long n) {
    for (long i = 0; i < n; i++)
        dst[i] = src[i];
}

static void test_sizet_copy(void) {
    long src[3] = {11, 22, 33};
    long dst[3] = {0};
    sizet_copy(dst, src, 3);
    for (int i = 0; i < 3; i++)
        if (dst[i] != src[i])
            __builtin_trap();
    passed++;
}

int main(void) {
    test_byte_copy();
    test_int_copy();
    test_long_copy();
    test_non_restrict_copy();
    test_zero_len();
    test_sizet_copy();
    if (passed != 6)
        return 1;
    return 42;
}
