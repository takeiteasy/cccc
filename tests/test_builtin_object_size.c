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

// ---------------------------------------------------------------------------
// Ternary (cond ? a : b) pointer: resolve both branches, combine with max or
// min depending on type bit 1.  Both branches must be statically resolvable;
// if either is unknown the conservative default is returned.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_ternary_type0_max(void) {
    // type 0 (whole object, max fallback): return max(100, 10) = 100.
    char big[100]; char small[10];
    int cond = 1;
    size_t sz = __builtin_object_size(cond ? big : small, 0);
    AssertEq((unsigned long long)sz, 100ULL);
}

[[cccc::test]]
void test_object_size_ternary_type1_max(void) {
    // type 1 (subobject, max fallback): bare arrays have sub == whole → 100.
    char big[100]; char small[10];
    int cond = 0;
    size_t sz = __builtin_object_size(cond ? big : small, 1);
    AssertEq((unsigned long long)sz, 100ULL);
}

[[cccc::test]]
void test_object_size_ternary_type2_min(void) {
    // type 2 (whole object, min fallback): return min(100, 10) = 10.
    char big[100]; char small[10];
    int cond = 1;
    size_t sz = __builtin_object_size(cond ? big : small, 2);
    AssertEq((unsigned long long)sz, 10ULL);
}

[[cccc::test]]
void test_object_size_ternary_type3_min(void) {
    // type 3 (subobject, min fallback): min of subobject sizes → 10.
    char big[100]; char small[10];
    int cond = 0;
    size_t sz = __builtin_object_size(cond ? big : small, 3);
    AssertEq((unsigned long long)sz, 10ULL);
}

[[cccc::test]]
void test_object_size_ternary_equal_branches(void) {
    // Equal-size branches: max == min == 32.
    char a[32]; char b[32];
    int cond = 1;
    AssertEq((unsigned long long)__builtin_object_size(cond ? a : b, 0), 32ULL);
    AssertEq((unsigned long long)__builtin_object_size(cond ? a : b, 2), 32ULL);
}

[[cccc::test]]
void test_object_size_ternary_with_offset(void) {
    // Ternary with constant offset on each branch.
    // &big[20]: 80 bytes remaining; &small[5]: 5 bytes remaining.
    // type 0 → max(80, 5) = 80; type 2 → min(80, 5) = 5.
    char big[100]; char small[10];
    int cond = 1;
    size_t s0 = __builtin_object_size(cond ? &big[20] : &small[5], 0);
    size_t s2 = __builtin_object_size(cond ? &big[20] : &small[5], 2);
    AssertEq((unsigned long long)s0, 80ULL);
    AssertEq((unsigned long long)s2, 5ULL);
}

[[cccc::test]]
void test_object_size_ternary_unknown_branch_conservative(void) {
    // One branch is a pointer variable (unknown) → conservative fallback.
    char buf[32];
    char *p = buf; // pointer var → unknown
    int cond = 1;
    size_t s0 = __builtin_object_size(cond ? buf : p, 0);
    size_t s2 = __builtin_object_size(cond ? buf : p, 2);
    AssertEq((unsigned long long)s0, (unsigned long long)(size_t)-1);
    AssertEq((unsigned long long)s2, 0ULL);
}

// ---------------------------------------------------------------------------
// Union members: base_size reflects the whole union (largest member); sub_size
// reflects the specific member accessed.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_union_small_member(void) {
    // u.a is char[4]; u.b is char[16] (largest).  Union size = 16.
    // type 0 (whole): 16 bytes from start of union.
    // type 1 (sub):    4 bytes (sizeof u.a).
    union { char a[4]; char b[16]; } u;
    size_t w = __builtin_object_size(&u.a, 0);
    size_t s = __builtin_object_size(&u.a, 1);
    AssertEq((unsigned long long)w, (unsigned long long)sizeof(u));
    AssertEq((unsigned long long)s, 4ULL);
}

[[cccc::test]]
void test_object_size_union_large_member(void) {
    // u.b is the largest member → sub == whole.
    union { char a[4]; char b[16]; } u;
    size_t w = __builtin_object_size(&u.b, 0);
    size_t s = __builtin_object_size(&u.b, 1);
    AssertEq((unsigned long long)w, (unsigned long long)sizeof(u));
    AssertEq((unsigned long long)s, 16ULL);
}
