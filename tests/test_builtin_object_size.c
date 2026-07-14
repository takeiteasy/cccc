// CCCC_FLAGS: --testing
// Tests for __builtin_object_size(ptr, type).
//
// type bits:
//   bit 0 = 0 → whole base object; bit 0 = 1 → nearest subobject
//   bit 1 = 0 → max fallback (size_t)-1; bit 1 = 1 → min fallback 0
//
// For objects of statically known size we compute the exact remaining bytes.
// For unknown pointers (function parameters, heap, etc.) we fall back to the
// conservative defaults ((size_t)-1 for type 0/1, 0 for type 2/3).

#include <stddef.h>

// ---------------------------------------------------------------------------
// Plain array (whole == subobject for a bare array)
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_array_type0(void) {
    char buf[64];
    size_t sz = __builtin_object_size(buf, 0);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_object_size_array_type1(void) {
    char buf[64];
    size_t sz = __builtin_object_size(buf, 1);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_object_size_array_type2(void) {
    char buf[64];
    size_t sz = __builtin_object_size(buf, 2);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_object_size_array_type3(void) {
    char buf[64];
    size_t sz = __builtin_object_size(buf, 3);
    AssertEq((unsigned long long)sz, 64ULL);
}

// ---------------------------------------------------------------------------
// Array with constant index: remaining bytes from &buf[k]
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_array_index(void) {
    char buf[64];
    // &buf[10] → 64 - 10 = 54 bytes remaining
    size_t sz = __builtin_object_size(&buf[10], 0);
    AssertEq((unsigned long long)sz, 54ULL);
}

[[cccc::test]]
void test_object_size_array_index_subobject(void) {
    char buf[64];
    // subobject is the array itself; 64 - 10 = 54
    size_t sz = __builtin_object_size(&buf[10], 1);
    AssertEq((unsigned long long)sz, 54ULL);
}

// ---------------------------------------------------------------------------
// Scalar variable
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_scalar(void) {
    int x = 0;
    size_t sz = __builtin_object_size(&x, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)sizeof(int));
}

// ---------------------------------------------------------------------------
// Struct member: whole object (type 0) vs subobject (type 1) differ
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_struct_whole_vs_sub(void) {
    struct { int a; char b[8]; } s;
    // type 0: bytes from &s.b to end of whole struct
    size_t whole = __builtin_object_size(&s.b, 0);
    // type 1: bytes remaining in member b itself (= sizeof(b) = 8)
    size_t sub   = __builtin_object_size(&s.b, 1);
    AssertEq((unsigned long long)sub, 8ULL);
    // whole >= sub (remaining in whole struct >= remaining in member)
    AssertTrue(whole >= sub);
    // whole == sizeof(s) - offsetof(b) = sizeof(s) - sizeof(int)
    AssertEq((unsigned long long)whole,
             (unsigned long long)(sizeof(s) - sizeof(int)));
}

// ---------------------------------------------------------------------------
// Conservative fallback for unknown pointers (function parameters)
// A function-parameter pointer has no compile-time backing object.
// ---------------------------------------------------------------------------

static void safe_memcpy(void *dst, const void *src, size_t len) {
    // dst is a function parameter → unknown → conservative (size_t)-1
    if (len > __builtin_object_size(dst, 0)) {
        // Would call __chk_fail; unreachable with conservative stub
        return;
    }
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

[[cccc::test]]
void test_unknown_ptr_type0_conservative(void) {
    // Conservative max: unknown pointer → (size_t)-1
    char buf[4];
    char *p = buf; // pointer variable, not a bare array → unknown
    size_t sz = __builtin_object_size(p, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_unknown_ptr_type2_conservative(void) {
    // Conservative min: unknown pointer → 0
    char buf[4];
    char *p = buf;
    size_t sz = __builtin_object_size(p, 2);
    AssertEq((unsigned long long)sz, 0ULL);
}
