// CCCC_FLAGS: --testing
// Tests for __builtin_object_size(ptr, type) conservative stub.
// type 0/1 → (size_t)-1 (unknown = max); type 2/3 → 0 (unknown = min).
// Stub is sufficient for _FORTIFY_SOURCE wrappers to compile and branch correctly.

#include <stddef.h>

[[cccc::test]]
void test_object_size_type0(void) {
    char buf[64];
    size_t sz = __builtin_object_size(buf, 0);
    // Conservative max: (size_t)-1
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_type1(void) {
    char buf[64];
    size_t sz = __builtin_object_size(buf, 1);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_type2(void) {
    char buf[64];
    size_t sz = __builtin_object_size(buf, 2);
    AssertEq((unsigned long long)sz, 0ULL);
}

[[cccc::test]]
void test_object_size_type3(void) {
    char buf[64];
    size_t sz = __builtin_object_size(buf, 3);
    AssertEq((unsigned long long)sz, 0ULL);
}

// Simulate a _FORTIFY_SOURCE-style wrapper: with the conservative stub,
// the size check (len > (size_t)-1) is always false for type 0,
// so the wrapper takes the safe path (calls the real function, not __chk_fail).
static void safe_memcpy(void *dst, const void *src, size_t len) {
    if (len > __builtin_object_size(dst, 0)) {
        // Would call __chk_fail; unreachable with conservative stub
        return;
    }
    // Safe path: proceed normally
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
}

[[cccc::test]]
void test_fortify_style_wrapper(void) {
    char dst[8];
    const char src[] = "hello";
    safe_memcpy(dst, src, 5);
    AssertEq(dst[0], 'h');
    AssertEq(dst[4], 'o');
}
