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
    char   buf[64];
    size_t sz = __builtin_object_size(buf, 0);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_object_size_array_type1(void) {
    char   buf[64];
    size_t sz = __builtin_object_size(buf, 1);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_object_size_array_type2(void) {
    char   buf[64];
    size_t sz = __builtin_object_size(buf, 2);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_object_size_array_type3(void) {
    char   buf[64];
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
    int    x  = 0;
    size_t sz = __builtin_object_size(&x, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)sizeof(int));
}

// ---------------------------------------------------------------------------
// Struct member: whole object (type 0) vs subobject (type 1) differ
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_struct_whole_vs_sub(void) {
    struct {
        int  a;
        char b[8];
    } s;
    // type 0: bytes from &s.b to end of whole struct
    size_t whole = __builtin_object_size(&s.b, 0);
    // type 1: bytes remaining in member b itself (= sizeof(b) = 8)
    size_t sub = __builtin_object_size(&s.b, 1);
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
    char       *d = (char *)dst;
    const char *s = (const char *)src;
    for (size_t i = 0; i < len; i++)
        d[i] = s[i];
}

[[cccc::test]]
void test_fortify_style_wrapper(void) {
    char       dst[8];
    const char src[] = "hello";
    safe_memcpy(dst, src, 5);
    AssertEq(dst[0], 'h');
    AssertEq(dst[4], 'o');
}

// #701: `char *p = buf;` (a plain array-base pointer variable, offset 0) is
// no longer an "unknown pointer" -- it's exactly the array-base derived-var
// case this ticket closes, and now resolves to sizeof(buf) like the bare
// array/direct-offset forms above. A genuinely unknown pointer needs a
// backing object with no compile-time provenance at all, e.g. a function
// parameter (see safe_memcpy's `dst` above) or an unannotated function's
// return value.
static char *test_objsize_opaque_ptr(char *in) {
    return in;
}

[[cccc::test]]
void test_unknown_ptr_type0_conservative(void) {
    // Conservative max: unknown pointer → (size_t)-1
    char  buf[4];
    char *p =
        test_objsize_opaque_ptr(buf); // routed through an opaque call → unknown
    size_t sz = __builtin_object_size(p, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_unknown_ptr_type2_conservative(void) {
    // Conservative min: unknown pointer → 0
    char   buf[4];
    char  *p  = test_objsize_opaque_ptr(buf);
    size_t sz = __builtin_object_size(p, 2);
    AssertEq((unsigned long long)sz, 0ULL);
}

// #701: the plain array-base derived-var case that replaced the two tests
// above -- `char *p = buf;` (offset 0) now resolves exactly, same as
// test_object_size_array_base_var_zero_offset below but placed here to keep
// the "conservative fallback" section's own regression coverage next to what
// it displaced.
[[cccc::test]]
void test_array_base_ptr_var_now_precise(void) {
    char   buf[4];
    char  *p  = buf;
    size_t sz = __builtin_object_size(p, 0);
    AssertEq((unsigned long long)sz, 4ULL);
}

// ---------------------------------------------------------------------------
// Ternary (cond ? a : b) pointer: resolve both branches, combine with max or
// min depending on type bit 1.  Both branches must be statically resolvable;
// if either is unknown the conservative default is returned.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_ternary_type0_max(void) {
    // type 0 (whole object, max fallback): return max(100, 10) = 100.
    char   big[100];
    char   small[10];
    int    cond = 1;
    size_t sz   = __builtin_object_size(cond ? big : small, 0);
    AssertEq((unsigned long long)sz, 100ULL);
}

[[cccc::test]]
void test_object_size_ternary_type1_max(void) {
    // type 1 (subobject, max fallback): bare arrays have sub == whole → 100.
    char   big[100];
    char   small[10];
    int    cond = 0;
    size_t sz   = __builtin_object_size(cond ? big : small, 1);
    AssertEq((unsigned long long)sz, 100ULL);
}

[[cccc::test]]
void test_object_size_ternary_type2_min(void) {
    // type 2 (whole object, min fallback): return min(100, 10) = 10.
    char   big[100];
    char   small[10];
    int    cond = 1;
    size_t sz   = __builtin_object_size(cond ? big : small, 2);
    AssertEq((unsigned long long)sz, 10ULL);
}

[[cccc::test]]
void test_object_size_ternary_type3_min(void) {
    // type 3 (subobject, min fallback): min of subobject sizes → 10.
    char   big[100];
    char   small[10];
    int    cond = 0;
    size_t sz   = __builtin_object_size(cond ? big : small, 3);
    AssertEq((unsigned long long)sz, 10ULL);
}

[[cccc::test]]
void test_object_size_ternary_equal_branches(void) {
    // Equal-size branches: max == min == 32.
    char a[32];
    char b[32];
    int  cond = 1;
    AssertEq((unsigned long long)__builtin_object_size(cond ? a : b, 0), 32ULL);
    AssertEq((unsigned long long)__builtin_object_size(cond ? a : b, 2), 32ULL);
}

[[cccc::test]]
void test_object_size_ternary_with_offset(void) {
    // Ternary with constant offset on each branch.
    // &big[20]: 80 bytes remaining; &small[5]: 5 bytes remaining.
    // type 0 → max(80, 5) = 80; type 2 → min(80, 5) = 5.
    char   big[100];
    char   small[10];
    int    cond = 1;
    size_t s0   = __builtin_object_size(cond ? &big[20] : &small[5], 0);
    size_t s2   = __builtin_object_size(cond ? &big[20] : &small[5], 2);
    AssertEq((unsigned long long)s0, 80ULL);
    AssertEq((unsigned long long)s2, 5ULL);
}

[[cccc::test]]
void test_object_size_ternary_unknown_branch_conservative(void) {
    // One branch is a pointer variable (unknown) → conservative fallback.
    char   buf[32];
    char  *p    = buf; // pointer var → unknown
    int    cond = 1;
    size_t s0   = __builtin_object_size(cond ? buf : p, 0);
    size_t s2   = __builtin_object_size(cond ? buf : p, 2);
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
    union {
        char a[4];
        char b[16];
    } u;
    size_t w = __builtin_object_size(&u.a, 0);
    size_t s = __builtin_object_size(&u.a, 1);
    AssertEq((unsigned long long)w, (unsigned long long)sizeof(u));
    AssertEq((unsigned long long)s, 4ULL);
}

[[cccc::test]]
void test_object_size_union_large_member(void) {
    // u.b is the largest member → sub == whole.
    union {
        char a[4];
        char b[16];
    } u;
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
    free(p);
}

[[cccc::test]]
void test_object_size_calloc_const(void) {
    // calloc(nmemb, size) → nmemb * size = 64.
    char *p = calloc(4, 16);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 64ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_realloc_const(void) {
    // realloc(ptr, size) tracks the *new* size; the source pointer being
    // reallocated is a plain expression (not the tracked variable), so it
    // doesn't interact with the poisoning rules.
    char *base = malloc(8);
    char *p    = realloc(base, 96);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 96ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_aligned_alloc_const(void) {
    char *p = aligned_alloc(16, 256);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 256ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_malloc_cast(void) {
    // A cast on the initializer is seen through.
    int *p = (int *)malloc(64);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 64ULL);
    free(p);
}

static void check_object_size_nonconst(size_t n) {
    // Non-constant size argument → cannot be tracked, conservative fallback.
    char *p = malloc(n);
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
    free(p);
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
    char *orphaned =
        p; // capture so the poisoned-over allocation can still be freed
    p = malloc(16); // reassignment poisons the tracked allocation
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
    free(orphaned);
    free(p);
}

[[cccc::test]]
void test_object_size_malloc_address_taken_conservative(void) {
    char  *p  = malloc(64);
    char **pp = &p; // address-of poisons the tracked allocation
    AssertTrue(pp != NULL);
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
    free(p);
}

[[cccc::test]]
void test_object_size_malloc_loop_reassign_conservative(void) {
    // The query is parsed *before* the reassignment inside the loop body,
    // but the reassignment is reachable at runtime via the loop back-edge on
    // iteration 2+. A parse-time fold would incorrectly return 128 for every
    // iteration; the post-parse poison scan must catch the later
    // reassignment and keep every query conservative.
    char              *p   = malloc(128);
    unsigned long long sum = 0;
    for (int i = 0; i < 3; i++) {
        sum            += __builtin_object_size(p, 0);
        char *orphaned  = p;
        p               = malloc(16);
        free(orphaned);
    }
    AssertEq(sum, 3ULL * (unsigned long long)(size_t)-1);
    free(p);
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
    char *p        = malloc(128);
    char *orphaned = p;
    void reassign(void) {
        p = malloc(4);
    }
    reassign();
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
    free(orphaned);
    free(p);
}

[[cccc::test]]
void test_object_size_malloc_block_reassign_conservative(void) {
    // Same hazard via an Apple block literal capturing a __block (by
    // reference) variable — plain by-value block captures can't alias the
    // outer pointer, but __block ones share the same Obj.
    __block char *p        = malloc(128);
    char         *orphaned = p;
    void (^reassign)(void) = ^{
      p = malloc(4);
    };
    reassign();
    AssertEq((unsigned long long)__builtin_object_size(p, 0),
             (unsigned long long)(size_t)-1);
    free(orphaned);
    free(p);
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
    char  *p        = malloc(128);
    char  *orphaned = p;
    size_t s        = 0;
    void q(void) {
        s = __builtin_object_size(p, 0);
    }
    q();
    AssertEq((unsigned long long)s, (unsigned long long)(size_t)-1);
    p = malloc(4);
    q();
    AssertEq((unsigned long long)s, (unsigned long long)(size_t)-1);
    free(orphaned);
    free(p);
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
void *test_objsize_arena_alloc(size_t n) {
    return malloc(n);
}

[[cccc::test]]
void test_object_size_custom_allocator_c23_syntax(void) {
    char *p = test_objsize_arena_alloc(96);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 96ULL);
    free(p);
}

// Same custom-allocator coverage via the GNU __attribute__((alloc_size(...)))
// spelling, exercising the two-index (calloc-style product) form.
__attribute__((alloc_size(1, 2), malloc)) void *
test_objsize_pool_calloc(size_t nmemb, size_t size);
void *test_objsize_pool_calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}

[[cccc::test]]
void test_object_size_custom_allocator_two_arg(void) {
    // 4 * 16 = 64, exercising the alloc_size(n,m) product form on a
    // non-libc function.
    char *p = test_objsize_pool_calloc(4, 16);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 64ULL);
    free(p);
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
    free(p);
}

// reallocarray(ptr, nmemb, size) -- alloc_size(2,3) product form. #642's
// name-based match for it was already dead code (nothing registered it as
// callable); it's wired up as of #699, so this now exercises the real thing.
[[cccc::test]]
void test_object_size_reallocarray_const(void) {
    char *base = malloc(8);
    char *p    = reallocarray(base, 4, 8);
    AssertEq((unsigned long long)__builtin_object_size(p, 0), 32ULL);
    free(p);
}

// ---------------------------------------------------------------------------
// #697: interior heap pointers (`p + k`) written *inline* in the builtin's
// argument.  Extends #642's deferred-query mechanism (not objsize_resolve_ptr
// -- see resolve_objsize_queries) so `__builtin_object_size(p + k, type)`
// resolves to `alloc_size - k` for an alloc-tracked, unpoisoned base pointer.
// See below (#700) for the intermediate-variable form (`char *q = p + k;
// __builtin_object_size(q, ...)`).
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_malloc_interior_type0(void) {
    char *p = malloc(128);
    // p + 32 -> 128 - 32 = 96 bytes remaining.
    AssertEq((unsigned long long)__builtin_object_size(p + 32, 0), 96ULL);
    AssertEq((unsigned long long)__builtin_object_size(p + 32, 1), 96ULL);
    AssertEq((unsigned long long)__builtin_object_size(p + 32, 2), 96ULL);
    AssertEq((unsigned long long)__builtin_object_size(p + 32, 3), 96ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_interior_zero_offset(void) {
    // offset 0 must behave exactly like the bare-var case (128).
    char *p = malloc(128);
    AssertEq((unsigned long long)__builtin_object_size(p + 0, 0), 128ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_interior_cast(void) {
    // Interior pointer through an intervening typed-pointer cast: 4 ints (16
    // bytes) into a 64-byte allocation, cast back to char* -> 48 remaining.
    // Inline form (no intermediate variable).
    char *p = malloc(64);
    AssertEq(
        (unsigned long long)__builtin_object_size((char *)((int *)p + 4), 0),
        48ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_interior_custom_allocator(void) {
    // Interior pointer into a buffer from an alloc_size-annotated custom
    // allocator -- confirms this is attribute-driven, not name-based.
    char *p = test_objsize_arena_alloc(96);
    AssertEq((unsigned long long)__builtin_object_size(p + 16, 0), 80ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_interior_past_end_conservative(void) {
    // Offset past the end of the allocation -> conservative fallback, not a
    // clamp to 0 (unlike the statically-known array path).
    char *p = malloc(128);
    AssertEq((unsigned long long)__builtin_object_size(p + 200, 0),
             (unsigned long long)(size_t)-1);
    AssertEq((unsigned long long)__builtin_object_size(p + 200, 2), 0ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_interior_reassigned_conservative(void) {
    // Soundness lock-in: the interior-pointer fold must ride the same
    // deferred poison-scan path as the bare-var case (#642), not a parse-time
    // fold in objsize_resolve_ptr -- otherwise this would incorrectly freeze
    // at 96 despite the later reassignment.
    char  *p        = malloc(128);
    char  *orphaned = p;
    size_t sz       = __builtin_object_size(p + 32, 0);
    p               = malloc(16);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
    free(orphaned);
    free(p);
}

// ---------------------------------------------------------------------------
// #700: interior heap pointers captured in an *intermediate variable*
// (`char *q = p + k;` then querying q), the form #697 explicitly left
// conservative. `q`'s tracked size is resolved lazily via
// Obj.objsize_derived_from (see objsize_effective_remaining in
// resolve_objsize_queries): q records a link to its base var and offset at
// declaration time, and the link is followed -- checking every ancestor's
// objsize_unsafe -- only once the whole function has been poison-scanned. This
// is required for soundness: p's value is only guaranteed constant for the
// whole function (and thus valid at q's initializer) when p itself is
// single-assignment and never address-taken, which can't be known until the
// full poison scan completes.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_object_size_derived_var_basic(void) {
    char *p = malloc(128);
    char *q = p + 32;
    // 128 - 32 = 96 remaining, for both the derived var itself and a further
    // inline offset on top of it.
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 96ULL);
    AssertEq((unsigned long long)__builtin_object_size(q + 8, 0), 88ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_chain(void) {
    // Multi-hop derivation: r derived from q, q derived from p.
    char *p = malloc(100);
    char *q = p + 10; // 90 remaining
    char *r = q + 20; // 70 remaining
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 90ULL);
    AssertEq((unsigned long long)__builtin_object_size(r, 0), 70ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_cast(void) {
    // Derivation through an intervening typed-pointer cast in the
    // initializer, mirroring #697's inline cast case.
    char *p = malloc(64);
    char *q = (char *)((int *)p + 4); // 16 bytes in -> 48 remaining
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 48ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_custom_allocator(void) {
    char *p = test_objsize_arena_alloc(96);
    char *q = p + 16;
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 80ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_nonconst_offset_conservative(void) {
    // Non-constant offset -> q is never registered as alloc-tracked at all.
    volatile int n = 8;
    char        *p = malloc(64);
    char        *q = p + n;
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_past_end_conservative(void) {
    char *p = malloc(64);
    char *q = p + 100; // past the end
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_own_reassignment_conservative(void) {
    // q itself is reassigned after derivation -> q is poisoned directly
    // (the pre-existing generic reassignment check applies to any
    // objsize_has_alloc var, derived or not).
    char *p = malloc(64);
    char *q = p + 8;
    q       = malloc(4);
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
    free(p);
    free(q);
}

[[cccc::test]]
void test_object_size_derived_var_own_address_taken_conservative(void) {
    char  *p  = malloc(64);
    char  *q  = p + 8;
    char **qq = &q;
    AssertTrue(qq != NULL);
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_base_reassigned_after_conservative(void) {
    // Base pointer p is reassigned *after* q derives from it -- q's tracked
    // size becomes stale (q's runtime value still points into the original
    // 128-byte allocation, but the derived-tracking invariant requires p to
    // be single-assignment for the whole function) so the query must stay
    // conservative, not silently return the original 96.
    char *p        = malloc(128);
    char *orphaned = p;
    char *q        = p + 32;
    p              = malloc(16);
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
    free(orphaned);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_base_reassigned_before_conservative(void) {
    // Base pointer p is reassigned *before* q is even declared -- p is
    // poisoned (assigned twice) by the time the whole function is
    // poison-scanned, so q's derived query must stay conservative even
    // though the reassignment is textually earlier than the derivation.
    char *p        = malloc(128);
    char *orphaned = p;
    p              = malloc(16);
    char *q        = p + 4;
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
    free(orphaned);
    free(p);
}

[[cccc::test]]
void test_object_size_derived_var_loop_reassign_conservative(void) {
    // Loop back-edge poisoning of the base var must propagate to a var
    // derived from it, mirroring #642's own loop-reassignment test.
    char              *m   = malloc(100);
    char              *n   = m + 5;
    unsigned long long sum = 0;
    for (int i = 0; i < 3; i++) {
        sum            += __builtin_object_size(n, 0);
        char *orphaned  = m;
        m               = malloc(4);
        free(orphaned);
    }
    AssertEq(sum, 3ULL * (unsigned long long)(size_t)-1);
    free(m);
}

// ---------------------------------------------------------------------------
// #701: two follow-up precision gaps left conservative by #697/#700.
//
// (1) Constant pointer *subtraction* was never peeled by
//     objsize_peel_offset_chain/objsize_resolve_ptr -- only ND_ADD was. `q -
//     const` (both the inline #697 form and the derived-variable #700 form)
//     now resolves exactly, as does a negative intermediate offset relative
//     to a derived var that is still non-negative relative to the true root
//     (e.g. `q - 16` where `q = p + 64` is 16 bytes before `q` but 48 bytes
//     into the real allocation).
//
// (2) An intermediate variable derived from a *statically-sized array* base
//     (`char *q = buf + k;`), not just a heap allocation, is now tracked the
//     same way #700 tracks a heap-derived var -- closing the gap between the
//     already-resolved direct form (`__builtin_object_size(buf + k, 0)`) and
//     the previously-conservative variable form.
// ---------------------------------------------------------------------------

static char objsize_701_gbuf[64];

[[cccc::test]]
void test_object_size_sub_inline(void) {
    char buf[64];
    // Peeled down to +12 before hitting the base var -- exercises the
    // ND_ADD/ND_SUB mix in a single chain.
    AssertEq((unsigned long long)__builtin_object_size(buf + 16 - 4, 0), 52ULL);
}

[[cccc::test]]
void test_object_size_sub_derived(void) {
    // The ticket's own example: q - 16 is 16 bytes *before* q, but q itself
    // is 64 bytes into a 128-byte allocation, so the true root-relative
    // offset is +48 -- verified against real clang/gcc.
    char *p = malloc(128);
    char *q = p + 64;
    AssertEq((unsigned long long)__builtin_object_size(q - 16, 0), 80ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_sub_derived_decl(void) {
    char *p = malloc(128);
    char *q = p + 64;
    char *r = q - 16; // r is itself now a derived var, root-relative offset 48
    AssertEq((unsigned long long)__builtin_object_size(r, 0), 80ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_sub_ptr_diff_conservative(void) {
    // ptr - ptr is an element count (node->ty == ty_long, no base type), not
    // a pointer offset -- must not be misread as one.
    char *p    = malloc(128);
    char *q    = p + 64;
    long  diff = q - p;
    AssertEq(diff, 64L);
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 64ULL);
    free(p);
}

[[cccc::test]]
void test_object_size_sub_underflow_conservative(void) {
    // p itself is the true root: p - 16 resolves to a negative root-relative
    // offset (before the allocation) and must stay conservative, not report
    // an oversized "remaining" count.
    char *p = malloc(128);
    char *r = p - 16;
    AssertEq((unsigned long long)__builtin_object_size(r, 0),
             (unsigned long long)(size_t)-1);
    free(p);
}

[[cccc::test]]
void test_object_size_array_base_var(void) {
    // The ticket's second example.
    char  buf[64];
    char *q = buf + 8;
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 56ULL);
}

[[cccc::test]]
void test_object_size_array_base_var_zero_offset(void) {
    char  buf[64];
    char *q = buf;
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 64ULL);
}

[[cccc::test]]
void test_object_size_array_base_global(void) {
    char *q = objsize_701_gbuf + 8;
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 56ULL);
}

[[cccc::test]]
void test_object_size_array_base_static(void) {
    static char sbuf[32];
    char       *q = sbuf + 4;
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 28ULL);
}

[[cccc::test]]
void test_object_size_array_base_chain(void) {
    char  buf[64];
    char *q = buf + 8; // 56 remaining
    char *r = q + 8;   // 48 remaining
    AssertEq((unsigned long long)__builtin_object_size(q, 0), 56ULL);
    AssertEq((unsigned long long)__builtin_object_size(r, 0), 48ULL);
}

[[cccc::test]]
void test_object_size_array_base_reassigned_conservative(void) {
    char  buf[64];
    char  other[16];
    char *q = buf + 8;
    q       = other; // q reassigned after derivation -> poisoned directly
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_array_base_addr_taken_conservative(void) {
    char   buf[64];
    char  *q  = buf + 8;
    char **qq = &q;
    AssertTrue(qq != NULL);
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_array_base_vla_conservative(void) {
    int   n = 32;
    char  vla[n];
    char *q = vla + 4; // VLA base has no compile-time size -> conservative
    AssertEq((unsigned long long)__builtin_object_size(q, 0),
             (unsigned long long)(size_t)-1);
}

[[cccc::test]]
void test_object_size_array_base_type1(void) {
    char  buf[64];
    char *q = buf + 8;
    // type 1: nearest subobject -- for a plain array, whole == subobject.
    AssertEq((unsigned long long)__builtin_object_size(q, 1), 56ULL);
}
