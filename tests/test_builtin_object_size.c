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
#include <stdlib.h>

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

// ---------------------------------------------------------------------------
// #642: constant malloc-family allocation tracking.
//
// A pointer that is assigned exactly once — its declaration initializer — to
// a malloc-family call with a compile-time constant size, and never
// reassigned or address-taken anywhere in the function, resolves to the real
// allocation size. This is resolved in a post-parse pass (after the whole
// function body is seen), so it correctly stays conservative for pointers
// that are reassigned later in the source, including across a loop
// back-edge — a naive parse-time fold would get this wrong. This is a
// GCC-compatible (but static-only; see __builtin_dynamic_object_size for the
// runtime-correct equivalent) extension for the "simple pattern" case
// documented in the ticket: direct assignment, no aliasing.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_malloc_const(void) {
    char *p = malloc(128);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 128ULL);
    AssertEq((unsigned long long)__builtin_object_size(p, 1), 128ULL);
    AssertEq((unsigned long long)__builtin_object_size(p, 2), 128ULL);
    AssertEq((unsigned long long)__builtin_object_size(p, 3), 128ULL);
}

[[cccc::test]]
void test_object_size_calloc_const(void) {
    // calloc(nmemb, size) → nmemb * size = 64.
    char *p = calloc(4, 16);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 64ULL);
}

[[cccc::test]]
void test_object_size_realloc_const(void) {
    // realloc(ptr, size) tracks the *new* size; the source pointer being
    // reallocated is a plain expression (not the tracked variable), so it
    // doesn't interact with the poisoning rules.
    char *base = malloc(8);
    char *p = realloc(base, 96);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 96ULL);
}

[[cccc::test]]
void test_object_size_aligned_alloc_const(void) {
    char *p = aligned_alloc(16, 256);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 256ULL);
}

[[cccc::test]]
void test_object_size_malloc_cast(void) {
    // A cast on the initializer is seen through.
    int *p = (int *)malloc(64);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 64ULL);
}

static void check_object_size_nonconst(size_t n) {
    // Non-constant size argument → cannot be tracked, conservative fallback.
    char *p = malloc(n);
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_malloc_nonconst_unresolved(void) {
    check_object_size_nonconst(32);
}

// ---------------------------------------------------------------------------
// Soundness lock-in: reassignment, address-of, and loop back-edges must all
// keep the query conservative rather than folding a stale size.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_malloc_reassigned_conservative(void) {
    char *p = malloc(128);
    p = malloc(16); // reassignment poisons the tracked allocation
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_malloc_address_taken_conservative(void) {
    char *p = malloc(64);
    char **pp = &p; // address-of poisons the tracked allocation
    AssertTrue(pp != NULL);
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_malloc_loop_reassign_conservative(void) {
    // The query is parsed *before* the reassignment inside the loop body,
    // but the reassignment is reachable at runtime via the loop back-edge on
    // iteration 2+. A parse-time fold would incorrectly return 128 for every
    // iteration; the post-parse poison scan must catch the later
    // reassignment and keep every query conservative.
    char *p = malloc(128);
    unsigned long long sum = 0;
    for (int i = 0; i < 3; i++) {
        sum += __builtin_object_size(p, 0);
        p = malloc(16);
    }
    AssertEq(sum, 3ULL * (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_malloc_nested_fn_reassign_conservative(void) {
    // A GNU nested function that reassigns a captured pointer accesses the
    // *same* Obj as the enclosing function (shared via the static-link
    // chain, not a by-value copy) — so this is really the same hazard as
    // direct reassignment, just reachable through a call rather than
    // straight-line code. resolve_objsize_queries must scan every function
    // body (nested ones included) so this poisoning lands before the
    // enclosing function's own query is resolved.
    char *p = malloc(128);
    void reassign(void) { p = malloc(4); }
    reassign();
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_malloc_block_reassign_conservative(void) {
    // Same hazard via an Apple block literal capturing a __block (by
    // reference) variable — plain by-value block captures can't alias the
    // outer pointer, but __block ones share the same Obj.
    __block char *p = malloc(128);
    void (^reassign)(void) = ^{ p = malloc(4); };
    reassign();
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_malloc_query_in_nested_fn_conservative(void) {
    // The query itself sits inside a nested function, on an enclosing-scope
    // pointer that is reassigned *after* the nested function is defined (but
    // still before the query re-runs at its second call). The nested
    // function's own resolve_objsize_queries fires the instant its body
    // finishes parsing — before the later `p = malloc(4)` in main is even
    // parsed — so if the query were allowed to register there it would
    // freeze at 128 and never see the reassignment. The query is only
    // registered when asked from the same function the pointer was declared
    // in (Obj.objsize_decl_fn), so this case must stay conservative on both
    // calls.
    char *p = malloc(128);
    size_t s = 0;
    void q(void) { s = __builtin_object_size(p, 0); }
    q();
    AssertEq((unsigned long long)s, (unsigned long long)(size_t)-1);
    p = malloc(4);
    q();
    AssertEq((unsigned long long)s, (unsigned long long)(size_t)-1);
}

// ---------------------------------------------------------------------------
// #649: __attribute__((alloc_size(...))) / __attribute__((malloc)).
//
// Generalizes #642's hardcoded malloc/calloc/realloc/reallocarray/
// aligned_alloc name matching to any function declared with the alloc_size
// attribute. The attribute is now the *sole* source of truth for
// objsize_alloc_from_call: libc's allocators self-describe via
// include/stdlib.h, custom allocators participate by declaring the
// attribute themselves, and a function that merely happens to share a
// malloc-family name but carries no attribute is correctly left untracked.
// ---------------------------------------------------------------------------

// A custom arena allocator, annotated the same way malloc is, using the C23
// [[gnu::alloc_size(...)]] / [[gnu::malloc]] spelling. Fixes #649's
// limitation #2: a project's own allocator wrapper now participates in
// __builtin_object_size sizing.
[[gnu::alloc_size(1), gnu::malloc]]
void *test_objsize_arena_alloc(size_t n);
void *test_objsize_arena_alloc(size_t n) { return malloc(n); }

[[cccc::test]]
void test_object_size_custom_allocator_c23_syntax(void) {
    char *p = test_objsize_arena_alloc(96);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 96ULL);
}

// Same custom-allocator coverage via the GNU __attribute__((alloc_size(...)))
// spelling, exercising the two-index (calloc-style product) form.
__attribute__((alloc_size(1, 2), malloc))
void *test_objsize_pool_calloc(size_t nmemb, size_t size);
void *test_objsize_pool_calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}

[[cccc::test]]
void test_object_size_custom_allocator_two_arg(void) {
    // 4 * 16 = 64, exercising the alloc_size(n,m) product form on a
    // non-libc function.
    char *p = test_objsize_pool_calloc(4, 16);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 64ULL);
}

// Fixes #649's limitation #1: a user function literally named "malloc" with
// a different signature/semantics and *no* alloc_size attribute must not be
// mistaken for the real allocator -- unlike #642's name-only matching, which
// only distinguished it by argument count.
static void *fake_malloc(int size) {
    static char buf[4];
    return buf;
}

[[cccc::test]]
void test_object_size_unannotated_malloc_lookalike_conservative(void) {
    void *p = fake_malloc(128);
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
}

// The real libc malloc/calloc/realloc/aligned_alloc still resolve through
// their include/stdlib.h alloc_size attribute (regression coverage for the
// authoritative-attribute rewrite of objsize_alloc_from_call).
[[cccc::test]]
void test_object_size_libc_malloc_via_attribute(void) {
    char *p = malloc(48);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 48ULL);
}

// reallocarray(ptr, nmemb, size) -- alloc_size(2,3) product form. #642's
// name-based match for it was already dead code (nothing registered it as
// callable); it's wired up as of #699, so this now exercises the real thing.
[[cccc::test]]
void test_object_size_reallocarray_const(void) {
    char *base = malloc(8);
    char *p = reallocarray(base, 4, 8);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 32ULL);
}
