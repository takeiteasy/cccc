// Test #268: restrict memcpy lowering for various element types.
// Verifies correctness after lowering across common element widths.

static int passed = 0;

static void short_copy(short *restrict dst, const short *restrict src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

static void test_short(void) {
    short src[4] = {-1, 0, 32767, -32768};
    short dst[4] = {0};
    short_copy(dst, src, 4);
    for (int i = 0; i < 4; i++)
        if (dst[i] != src[i]) __builtin_trap();
    passed++;
}

static void uint_copy(unsigned *restrict dst, const unsigned *restrict src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

static void test_uint(void) {
    unsigned src[3] = {0, 0xFFFFFFFFu, 42};
    unsigned dst[3] = {0};
    uint_copy(dst, src, 3);
    for (int i = 0; i < 3; i++)
        if (dst[i] != src[i]) __builtin_trap();
    passed++;
}

static void ulong_copy(unsigned long *restrict dst,
                       const unsigned long *restrict src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

static void test_ulong(void) {
    unsigned long src[2] = {0xDEADBEEFCAFEBABEUL, 0UL};
    unsigned long dst[2] = {0};
    ulong_copy(dst, src, 2);
    for (int i = 0; i < 2; i++)
        if (dst[i] != src[i]) __builtin_trap();
    passed++;
}

// Pointer copy
static void ptr_copy(void **restrict dst, void *const *restrict src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

static void test_ptr(void) {
    int a, b, c;
    void *src[3] = {&a, &b, &c};
    void *dst[3] = {0};
    ptr_copy(dst, src, 3);
    for (int i = 0; i < 3; i++)
        if (dst[i] != src[i]) __builtin_trap();
    passed++;
}

int main(void) {
    test_short();
    test_uint();
    test_ulong();
    test_ptr();
    if (passed != 4)
        return 1;
    return 42;
}
