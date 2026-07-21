// CCCC_FLAGS: --testing
// Tests for __builtin_dynamic_object_size(ptr, type).
//
// type bits (same encoding as __builtin_object_size):
//   bit 0 = 0 → whole base object; bit 0 = 1 → nearest subobject
//   bit 1 = 0 → max fallback (size_t)-1; bit 1 = 1 → min fallback 0
//
// Static fold: when the backing object is statically known (stack/global
// array, constant-offset chain) the result is computed at compile time
// (identical to __builtin_object_size).
//
// Runtime path: uses the VM heap (-V / --vm-heap) so that malloc/calloc/
// realloc are routed through the MALC/CALC/REALC opcodes which write an
// AllocHeader before each allocation and record the base address in
// vm->sorted_allocs.  DYNOBJSZ binary-searches sorted_allocs for the
// allocation containing the pointer (base or interior) and returns
// AllocHeader.requested_size - offset.
//
// Conservative fallback: function-parameter stack pointers, freed pointers,
// out-of-bounds interior pointers (past requested_size, e.g. into alignment
// padding), and non-VM-heap pointers all return (size_t)-1 (type 0/1) or 0
// (type 2/3).

#include <stddef.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Static fold — same as __builtin_object_size for statically-known objects.
// No --vm-heap needed: these are resolved at compile time.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_dynobj_static_array_type0(void) {
    char buf[64];
    size_t sz = __builtin_dynamic_object_size(buf, 0);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_dynobj_static_array_type2(void) {
    char buf[64];
    size_t sz = __builtin_dynamic_object_size(buf, 2);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_dynobj_static_array_offset(void) {
    char buf[64];
    // &buf[10] → 64 - 10 = 54 bytes remaining
    size_t sz = __builtin_dynamic_object_size(&buf[10], 0);
    AssertEq((unsigned long long)sz, 54ULL);
}

[[cccc::test]]
void test_dynobj_static_scalar(void) {
    int x = 0;
    size_t sz = __builtin_dynamic_object_size(&x, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)sizeof(int));
}

[[cccc::test]]
void test_dynobj_static_struct_subobject(void) {
    struct { int a; char b[8]; } s;
    // type 1: subobject (the member b) = 8 bytes
    size_t sub = __builtin_dynamic_object_size(&s.b, 1);
    AssertEq((unsigned long long)sub, 8ULL);
    // type 0: whole object remaining from &s.b
    size_t whole = __builtin_dynamic_object_size(&s.b, 0);
    AssertEq((unsigned long long)whole,
             (unsigned long long)(sizeof(s) - sizeof(int)));
}

// ---------------------------------------------------------------------------
// Runtime heap sizing via DYNOBJSZ + AllocHeader.
// Requires --vm-heap so malloc/calloc/realloc go through MALC/CALC/REALC
// and write the AllocHeader that DYNOBJSZ reads at runtime.
// ---------------------------------------------------------------------------

[[cccc::test(flags = "-V")]]
void test_dynobj_heap_malloc_type0(void) {
    char *p = malloc(64);
    AssertNotNull(p);
    size_t sz = __builtin_dynamic_object_size(p, 0);
    AssertEq((unsigned long long)sz, 64ULL);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_heap_malloc_type1(void) {
    // type 1 (subobject): for a base pointer the subobject IS the whole
    // allocation — same result as type 0.
    char *p = malloc(128);
    AssertNotNull(p);
    size_t sz = __builtin_dynamic_object_size(p, 1);
    AssertEq((unsigned long long)sz, 128ULL);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_heap_malloc_type2(void) {
    // type 2: fallback for unknowns is 0, but VM heap base pointers are
    // known at runtime → returns requested_size.
    char *p = malloc(32);
    AssertNotNull(p);
    size_t sz = __builtin_dynamic_object_size(p, 2);
    AssertEq((unsigned long long)sz, 32ULL);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_heap_malloc_type3(void) {
    // type 3: subobject + min fallback.
    char *p = malloc(16);
    AssertNotNull(p);
    size_t sz = __builtin_dynamic_object_size(p, 3);
    AssertEq((unsigned long long)sz, 16ULL);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_heap_calloc(void) {
    int *p = calloc(10, sizeof(int));
    AssertNotNull(p);
    size_t sz = __builtin_dynamic_object_size(p, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)(10 * sizeof(int)));
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_heap_realloc(void) {
    char *p = malloc(32);
    AssertNotNull(p);
    p = realloc(p, 128);
    AssertNotNull(p);
    size_t sz = __builtin_dynamic_object_size(p, 0);
    AssertEq((unsigned long long)sz, 128ULL);
    free(p);
}

// #699: reallocarray routes through REALCA -> REALC -> MALC on the VM-heap
// path, so the resulting allocation gets a real AllocHeader and is recorded
// in sorted_allocs exactly like realloc's -- this is the proof that REALCA
// delivers the heap-safety parity it was added for, not just a working
// return value.
[[cccc::test]]
void test_dynobj_heap_reallocarray(void) {
    int *base = malloc(4 * sizeof(int));
    AssertNotNull(base);
    int *p = reallocarray(base, 8, sizeof(int));
    AssertNotNull(p);
    size_t sz = __builtin_dynamic_object_size(p, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)(8 * sizeof(int)));
    free(p);
}

// ---------------------------------------------------------------------------
// Conservative fallback for unknown / non-base-heap pointers.
// These do not require --vm-heap: the fallback path fires for any pointer
// that is not a base pointer into the VM heap.
// ---------------------------------------------------------------------------

// Helper: function-parameter pointer has no statically-known backing object
// and is a stack address (not in the VM heap) → conservative fallback.
static size_t param_obj_size(void *dst) {
    return __builtin_dynamic_object_size(dst, 0);
}

[[cccc::test]]
void test_dynobj_param_ptr_type0(void) {
    char buf[8];
    // Stack address passed through a function parameter.
    // DYNOBJSZ: not in VM heap → conservative max (size_t)-1.
    size_t sz = param_obj_size(buf);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
}

static size_t param_obj_size_type2(void *dst) {
    return __builtin_dynamic_object_size(dst, 2);
}

[[cccc::test]]
void test_dynobj_param_ptr_type2(void) {
    char buf[8];
    // type 2 conservative min fallback = 0
    size_t sz = param_obj_size_type2(buf);
    AssertEq((unsigned long long)sz, 0ULL);
}

// ---------------------------------------------------------------------------
// Interior heap pointers (p + k) — resolved via the vm->sorted_allocs
// base-address range query (binary search for the containing allocation).
// ---------------------------------------------------------------------------

[[cccc::test(flags = "-V")]]
void test_dynobj_interior_heap_ptr_type0(void) {
    char *p = malloc(64);
    AssertNotNull(p);
    char *interior = p + 10;
    // 64 - 10 = 54 bytes remaining.
    size_t sz = __builtin_dynamic_object_size(interior, 0);
    AssertEq((unsigned long long)sz, 54ULL);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_interior_heap_ptr_type2(void) {
    char *p = malloc(64);
    AssertNotNull(p);
    char *interior = p + 40;
    size_t sz = __builtin_dynamic_object_size(interior, 2);
    AssertEq((unsigned long long)sz, 24ULL);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_interior_heap_ptr_last_byte(void) {
    // Pointer to the very last valid byte: 1 byte remaining.
    char *p = malloc(32);
    AssertNotNull(p);
    char *interior = p + 31;
    size_t sz = __builtin_dynamic_object_size(interior, 0);
    AssertEq((unsigned long long)sz, 1ULL);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_interior_heap_ptr_out_of_bounds_conservative(void) {
    // Pointer past the end of the requested allocation (e.g. into 8-byte
    // alignment padding) is out of bounds → conservative fallback, never a
    // false (too-large) claim.
    char *p = malloc(3);
    AssertNotNull(p);
    char *past_end = p + 8; // rounded allocation is 8 bytes, requested is 3
    size_t sz = __builtin_dynamic_object_size(past_end, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_interior_heap_ptr_freed_conservative(void) {
    // Interior pointer into a freed allocation → conservative fallback.
    char *p = malloc(64);
    AssertNotNull(p);
    char *interior = p + 10;
    free(p);
    size_t sz = __builtin_dynamic_object_size(interior, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_multiple_allocs_interior_lookup(void) {
    // Ensures the binary search picks the correct allocation among several.
    char *a = malloc(16);
    char *b = malloc(32);
    char *c = malloc(64);
    AssertNotNull(a);
    AssertNotNull(b);
    AssertNotNull(c);

    size_t sz_a = __builtin_dynamic_object_size(a + 4, 0);
    size_t sz_b = __builtin_dynamic_object_size(b + 4, 0);
    size_t sz_c = __builtin_dynamic_object_size(c + 4, 0);
    AssertEq((unsigned long long)sz_a, 12ULL);
    AssertEq((unsigned long long)sz_b, 28ULL);
    AssertEq((unsigned long long)sz_c, 60ULL);

    free(a);
    free(b);
    free(c);
}

// ---------------------------------------------------------------------------
// FORTIFY_SOURCE-style wrapper using the dynamic size.
//
// Primary use-case: a bounds-checking memcpy wrapper that uses
// __builtin_dynamic_object_size to validate at runtime when dst is a VM
// heap pointer whose size is not statically known.
// ---------------------------------------------------------------------------

static void dynamic_safe_memcpy(void *dst, const void *src, size_t len) {
    size_t avail = __builtin_dynamic_object_size(dst, 0);
    if (avail != (size_t)-1 && len > avail) {
        // Would call __chk_fail in a real FORTIFY implementation.
        return;
    }
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (size_t i = 0; i < len; i++) d[i] = s[i];
}

[[cccc::test(flags = "-V")]]
void test_dynobj_fortify_style_heap(void) {
    char *dst = malloc(16);
    AssertNotNull(dst);
    const char src[] = "hello";
    dynamic_safe_memcpy(dst, src, 5);
    // __builtin_dynamic_object_size(dst, 0) returns 16 → copy proceeds.
    AssertEq(dst[0], 'h');
    AssertEq(dst[4], 'o');
    free(dst);
}
