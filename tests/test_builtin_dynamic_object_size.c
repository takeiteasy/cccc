// CCCC_FLAGS: --testing
// CCCC_LEAKS_KEEP_VM_HEAP: DYNOBJSZ needs AllocHeader; degrades to (size_t)-1
// without it Tests for __builtin_dynamic_object_size(ptr, type).
//
// type bits (same encoding as __builtin_object_size):
//   bit 0 = 0 → whole base object; bit 0 = 1 → nearest subobject
//   bit 1 = 0 → max fallback (size_t)-1; bit 1 = 1 → min fallback 0
//
// Static fold: when the backing object is statically known (stack/global
// array, constant-offset chain) the result is computed at compile time
// (identical to __builtin_object_size).
//
// Runtime path, heap: uses the VM heap (-V / --no-vm-heap) so that malloc/
// calloc/realloc are routed through the MALC/CALC/REALC opcodes which write
// an AllocHeader before each allocation and record the base address in
// vm->sorted_allocs.  DYNOBJSZ binary-searches sorted_allocs for the
// allocation containing the pointer (base or interior) and returns
// AllocHeader.requested_size - offset.  alloca()/VLA buffers also resolve
// through this path: both lower to ALCA (a VM heap bump allocation with the
// same AllocHeader/sorted_allocs shape as MALC, just tagged
// AllocHeader.is_internal so leak detection skips it -- #979), so they carry
// a full AllocHeader with no separate mechanism needed (#648).
//
// Runtime path, stack: fixed-size stack arrays/structs/unions whose address
// escapes (e.g. passed through a function parameter, so the pointer's
// provenance is opaque by the time DYNOBJSZ sees it) resolve via
// vm->stack_intervals, the stack analogue of sorted_allocs (#675, extended
// by #648). The compiler emits STKTAG right after any escaping aggregate
// local's address is materialized, recording [base, base+size) tagged with
// the creating frame's epoch; DYNOBJSZ stabs that table and trusts the
// match only while its frame's epoch is still live.  Using this builtin at
// all (regardless of --dangling-detection) is what activates the epoch/
// interval bookkeeping needed for this path -- but only for the functions
// that actually need it: a function's ENT3 pushes its own frame epoch only
// when its body emits STKTAG (an escaping aggregate local/param) or a
// recorded LEA3 (an escaping scalar, under --dangling-detection); a frame
// with no escaping local/param of its own pushes nothing and is simply
// absent from vm->frame_epochs for its entire activation.
//
// Conservative fallback: freed pointers, out-of-bounds interior pointers
// (past requested_size, e.g. into alignment padding), dangling pointers into
// a stack frame that has already returned, and any other pointer with no
// resolvable provenance all return (size_t)-1 (type 0/1) or 0 (type 2/3).

#include <stddef.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Static fold — same as __builtin_object_size for statically-known objects.
// No -V/--no-vm-heap needed: these are resolved at compile time.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_dynobj_static_array_type0(void) {
    char   buf[64];
    size_t sz = __builtin_dynamic_object_size(buf, 0);
    AssertEq((unsigned long long)sz, 64ULL);
}

[[cccc::test]]
void test_dynobj_static_array_type2(void) {
    char   buf[64];
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
void test_dynobj_static_array_sub_inline(void) {
    // #701: constant ND_SUB is now peeled by objsize_resolve_ptr, same as
    // __builtin_object_size's static fold.
    char   buf[64];
    size_t sz = __builtin_dynamic_object_size(buf + 16 - 4, 0);
    AssertEq((unsigned long long)sz, 52ULL);
}

[[cccc::test]]
void test_dynobj_static_scalar(void) {
    int    x  = 0;
    size_t sz = __builtin_dynamic_object_size(&x, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)sizeof(int));
}

[[cccc::test]]
void test_dynobj_static_struct_subobject(void) {
    struct {
        int  a;
        char b[8];
    } s;
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
// Requires the VM heap (i.e. no -V/--no-vm-heap) so malloc/calloc/realloc
// go through MALC/CALC/REALC
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
// Runtime stack path (#648): a fixed-size stack array whose address escapes
// through a function parameter resolves via vm->stack_intervals (STKTAG),
// not the conservative fallback -- the escaping local's extent was tagged
// with its creating frame's epoch, and that frame is still live here.
// These do not require the VM heap: the stack path is independent of it.
// ---------------------------------------------------------------------------

// Helper: function-parameter pointer has no statically-known backing object,
// so this exercises the runtime path rather than the compile-time fold.
static size_t param_obj_size(void *dst) {
    return __builtin_dynamic_object_size(dst, 0);
}

[[cccc::test]]
void test_dynobj_param_ptr_type0(void) {
    char buf[8];
    // Stack address passed through a function parameter. Not in the VM
    // heap, but buf's [base, base+8) extent is tracked in stack_intervals
    // (STKTAG) and buf's frame is still live → resolves to the real size.
    size_t sz = param_obj_size(buf);
    AssertEq((unsigned long long)sz, 8ULL);
}

static size_t param_obj_size_type2(void *dst) {
    return __builtin_dynamic_object_size(dst, 2);
}

[[cccc::test]]
void test_dynobj_param_ptr_type2(void) {
    char buf[8];
    // type 2 (min fallback 0 on failure) still resolves to the real size
    // when the stack path succeeds -- the fallback direction only matters
    // for pointers DYNOBJSZ cannot resolve at all.
    size_t sz = param_obj_size_type2(buf);
    AssertEq((unsigned long long)sz, 8ULL);
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
    char  *interior = p + 40;
    size_t sz       = __builtin_dynamic_object_size(interior, 2);
    AssertEq((unsigned long long)sz, 24ULL);
    free(p);
}

[[cccc::test(flags = "-V")]]
void test_dynobj_interior_heap_ptr_last_byte(void) {
    // Pointer to the very last valid byte: 1 byte remaining.
    char *p = malloc(32);
    AssertNotNull(p);
    char  *interior = p + 31;
    size_t sz       = __builtin_dynamic_object_size(interior, 0);
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
    char  *past_end = p + 8; // rounded allocation is 8 bytes, requested is 3
    size_t sz       = __builtin_dynamic_object_size(past_end, 0);
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
    char       *d = (char *)dst;
    const char *s = (const char *)src;
    for (size_t i = 0; i < len; i++)
        d[i] = s[i];
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

// ---------------------------------------------------------------------------
// alloca()/VLA buffers (#648 follow-up to #640). Both lower to ALCA (a VM
// heap bump allocation with a full AllocHeader, #979), so they resolve
// through the same sorted_allocs path as malloc -- no new mechanism, just a
// regression lock proving it. No -V/--no-vm-heap needed: ALCA always carries
// an AllocHeader.
// ---------------------------------------------------------------------------

[[cccc::test]]
void test_dynobj_alloca_base_and_interior(void) {
    char *p = __builtin_alloca(20);
    AssertEq((unsigned long long)__builtin_dynamic_object_size(p, 0), 20ULL);
    AssertEq((unsigned long long)__builtin_dynamic_object_size(p + 5, 0),
             15ULL);
}

[[cccc::test]]
void test_dynobj_alloca_type2(void) {
    char *p = __builtin_alloca(20);
    // type 2 (min fallback 0) still resolves to the real size when the
    // pointer is found -- the fallback direction only matters on a miss.
    AssertEq((unsigned long long)__builtin_dynamic_object_size(p, 2), 20ULL);
}

static size_t vla_dynobjsize(int n) {
    char buf[n]; // VLA -- lowered to alloca(vla_size)
    return __builtin_dynamic_object_size(buf, 0);
}

static size_t vla_dynobjsize_interior(int n, int k) {
    char buf[n];
    return __builtin_dynamic_object_size(buf + k, 0);
}

[[cccc::test]]
void test_dynobj_vla_base_and_interior(void) {
    AssertEq((unsigned long long)vla_dynobjsize(10), 10ULL);
    AssertEq((unsigned long long)vla_dynobjsize_interior(10, 3), 7ULL);
}

// ---------------------------------------------------------------------------
// Escaping fixed-size stack aggregates (#648): resolved via
// vm->stack_intervals (STKTAG), not sorted_allocs -- these are real stack
// memory, never routed through MALC.
// ---------------------------------------------------------------------------

typedef struct {
    int a[4];
} DynobjStruct16;

static size_t struct_obj_size(void *dst) {
    return __builtin_dynamic_object_size(dst, 0);
}

[[cccc::test]]
void test_dynobj_stack_struct_via_param(void) {
    DynobjStruct16 s;
    size_t         sz = struct_obj_size(&s);
    AssertEq((unsigned long long)sz,
             (unsigned long long)sizeof(DynobjStruct16));
}

// A pointer into a frame that has already returned must NOT resolve to a
// stale size: stack_intervals retains dead-frame entries (addresses are
// reused, never pruned, #675), so DYNOBJSZ must reject a match whose epoch
// is no longer in vm->live_epochs and fall back to conservative.
static char *dangling_stack_ptr(void) {
    char buf[24];
    return buf; // returning address of a local -- the pointer outlives it
}

[[cccc::test]]
void test_dynobj_stack_dangling_conservative(void) {
    char  *p  = dangling_stack_ptr();
    size_t sz = __builtin_dynamic_object_size(p, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
}

// ---------------------------------------------------------------------------
// Activation without --dangling-detection (#648): using the builtin at all
// turns on the same frame_epochs/stack_intervals bookkeeping #673/#675 use,
// independently of -1/-2/-3. These exercise that mode specifically (no
// CCCC_FLAGS safety level set) through recursion and a multi-frame longjmp,
// to prove ENT3/LEV3 push/pop stays symmetric and the LEV3 tripwire
// (chain_depth == frame_epochs.count) holds when this is the only consumer.
// ---------------------------------------------------------------------------

// Routed through param_obj_size (declared above) rather than calling
// __builtin_dynamic_object_size(buf, 0) directly: a directly-named local
// array statically folds (same resolver __builtin_object_size uses) and
// never reaches the DYNOBJSZ opcode at all. Passing through an opaque
// pointer parameter is what forces the runtime stack path.
//
// The base case deliberately assigns param_obj_size(buf)'s result to a local
// before returning it, rather than `return param_obj_size(buf);` directly:
// that tail-call shape lets the optimizer lower it to CALLT, which retires
// *this* frame's epoch before the callee runs (correct: TCO hands this
// frame's stack memory to the callee immediately, so a still-live epoch
// here would be a lie -- see man/SAFETY.md's #675 interval note). A
// non-tail-call keeps this frame nested and its epoch live for the
// duration of the call, which is what this test means to exercise.
static size_t recurse_and_size(int depth) {
    char buf[8];
    if (depth == 0) {
        size_t sz = param_obj_size(buf);
        return sz;
    }
    size_t inner = recurse_and_size(depth - 1);
    // Also size this frame's own buffer on the way back out, after several
    // inner frames have pushed and popped their epochs.
    size_t mine = param_obj_size(buf);
    return (mine == 8 && inner == 8) ? 8 : 0;
}

[[cccc::test]]
void test_dynobj_stack_recursion_activation(void) {
    AssertEq((unsigned long long)recurse_and_size(20), 8ULL);
}

// ---------------------------------------------------------------------------
// #703: lazy per-function frame-epoch activation. A function's ENT3 now
// pushes a frame epoch only when its own body emits STKTAG (an escaping
// aggregate local/param) or a recorded LEA3 (an escaping scalar, under
// --dangling-detection) -- not unconditionally for every call once DYNOBJSZ
// is present anywhere. A frame that pushes nothing simply isn't in
// vm->frame_epochs; op_LEV3_fn's bp-matched pop then no-ops for it. These
// lock that behavior directly: an escaping struct *parameter* (which lives
// on fn->params, not fn->locals -- the case a locals-only scan would have
// missed), and a call chain mixing pushing and non-pushing frames.
// ---------------------------------------------------------------------------

typedef struct {
    int a[4];
} DynobjParamStruct;

// s is a parameter of this function, not a local -- its address escaping
// here (via struct_obj_size(&s), an ND_FUNCALL argument) must still mark
// s->addr_escapes and emit STKTAG for it, and this function's ENT3 must
// push its own epoch as a result.
static size_t escaping_param_size(DynobjParamStruct s) {
    return struct_obj_size(&s);
}

[[cccc::test]]
void test_dynobj_escaping_aggregate_param(void) {
    DynobjParamStruct s  = {{1, 2, 3, 4}};
    size_t            sz = escaping_param_size(s);
    AssertEq((unsigned long long)sz,
             (unsigned long long)sizeof(DynobjParamStruct));
}

// Plain pass-through wrappers: none takes the address of any local/param of
// its own, so none of them emits STKTAG or a recorded LEA3 -- none pushes a
// frame epoch (#703). The escaping buffer's *owner* frame (whichever caller
// actually took &buf) is what the DYNOBJSZ resolution inside chain_c relies
// on staying live; the non-pushing frames in between are simply absent from
// vm->frame_epochs throughout.
static size_t chain_a(void *p);
static size_t chain_b(void *p);
static size_t chain_c(void *p);

static size_t chain_a(void *p) {
    return chain_b(p);
}
static size_t chain_b(void *p) {
    return chain_c(p);
}
static size_t chain_c(void *p) {
    return __builtin_dynamic_object_size(p, 0);
}

[[cccc::test]]
void test_dynobj_mixed_chain_owner_alive(void) {
    char buf[12];
    // buf's address escapes into chain_a -- this test function's frame
    // pushes an epoch; chain_a/b/c push none of their own.
    size_t sz = chain_a(buf);
    AssertEq((unsigned long long)sz, 12ULL);
}

static char *chain_owner_returns(void) {
    char buf[12];
    (void)chain_a(buf); // buf's frame pushes; chain_a/b/c do not
    return buf;         // now dangling -- its owner frame is about to retire
}

[[cccc::test]]
void test_dynobj_mixed_chain_owner_stale_conservative(void) {
    char *p = chain_owner_returns();
    // The owner frame (chain_owner_returns) has returned and its epoch
    // retired; a stale match must be rejected even though the intermediate
    // non-pushing frames (chain_a/b/c) never touched live_epochs at all.
    size_t sz = __builtin_dynamic_object_size(p, 0);
    AssertEq((unsigned long long)sz, (unsigned long long)(size_t)-1);
}

#include <setjmp.h>

static jmp_buf dynobj_jmp_env;

static void dynobj_jmp_level3(void) {
    char buf[8];
    (void)param_obj_size(buf);  // opaque call -- forces the runtime path
    longjmp(dynobj_jmp_env, 1); // unwinds level3 + level2 in one jump
}

static void dynobj_jmp_level2(void) {
    char buf[16];
    (void)param_obj_size(buf);
    dynobj_jmp_level3();
}

[[cccc::test]]
void test_dynobj_stack_longjmp_activation(void) {
    char buf[32];
    if (setjmp(dynobj_jmp_env) == 0) {
        dynobj_jmp_level2();
        Assert(0); // unreachable
    }
    // Back in the test function after the multi-frame unwind. buf must
    // still resolve correctly -- proves frame_epoch_truncate_to retired
    // exactly the unwound frames and left this one's epoch live.
    size_t sz = param_obj_size(buf);
    AssertEq((unsigned long long)sz, 32ULL);
}

// #703: the same multi-frame longjmp, but with non-pushing wrapper frames
// (no address-of-local of their own) interleaved between the pushing ones.
// frame_epoch_truncate_to is bp-driven, so it must unwind exactly the
// pushing frames it finds among them (dynobj_jmp_leaf) and land cleanly on
// the setjmp frame, without needing every intermediate frame to have pushed.
static jmp_buf dynobj_jmp_env2;

static void dynobj_jmp_leaf(void) {
    char buf[8];
    (void)param_obj_size(buf); // this frame pushes (owns an escaping local)
    longjmp(dynobj_jmp_env2, 1);
}

static void dynobj_jmp_wrapper2(void) {
    dynobj_jmp_leaf();
}
static void dynobj_jmp_wrapper1(void) {
    dynobj_jmp_wrapper2();
}

[[cccc::test]]
void test_dynobj_longjmp_across_nonpushing_frames(void) {
    char buf[40];
    if (setjmp(dynobj_jmp_env2) == 0) {
        dynobj_jmp_wrapper1();
        Assert(0); // unreachable
    }
    size_t sz = param_obj_size(buf);
    AssertEq((unsigned long long)sz, 40ULL);
}
