// CCCC_FLAGS: --testing -O1
// CCCC_MATRIX_SKIP: exercises CALLT tail-call codegen, which requires -O1;
// the per-pass matrix forces -O0 and would trivially pass without exercising
// the bug this test guards against.
//
// #718: follow-up to #716's CALLT frame-reuse guard (can_emit_tail_call /
// tail_arg_carries_frame_addr in codegen.c). That guard's second leg treats
// any local already marked Obj->addr_escapes (set by mark_addr_escapes /
// find_and_mark_escaping_addr in parse.c) as unsafe to tail-call with. But
// find_and_mark_escaping_addr's ND_ASSIGN handling only recursed into an
// escaping sink for ND_ADDR (`&x`) and array/struct/union decay -- pointer
// arithmetic on a frame-local base reaching an escaping sink through an
// assignment, e.g.:
//
//   int *q = buf + 0;   // buf's escape was never marked:
//   find_and_mark_escaping_addr's return use(q);      // switch had no
//   ND_ADD/ND_SUB case, so it fell to
//                        // "not an address" and returned without marking
//
// left `buf`'s escaping-ness unmarked, so the tail call into `use` was
// wrongly still eligible. `q` itself isn't recognized by #716's per-argument
// walk either (it's just a bare pointer variable, not a literal &-chain at
// the call site), so this depended entirely on the addr_escapes net -- and
// that net had a hole. Fixed by adding an ND_ADD/ND_SUB case to
// find_and_mark_escaping_addr that delegates to mark_escaping_root (which
// already walks pointer arithmetic for the ND_ADDR case).
//
// Each unsafe `use_*` clobbers its own (potentially reused) frame directly
// in its body before dereferencing -- a nested helper call doesn't reliably
// land on the same memory the caller's escaped local occupied (see #716's
// test file), so an in-body write is required to make corruption
// deterministic rather than layout-dependent.

#include <stddef.h>

static volatile long g_sink;
#define CLOBBER_FRAME()                                                        \
    do {                                                                       \
        volatile long junk[8];                                                 \
        for (int _i = 0; _i < 8; _i++)                                         \
            junk[_i] = 0xdeaddead + _i;                                        \
        g_sink = junk[0]; /* keep the writes from being optimized away */      \
    } while (0)

static int chk(int n) {
    return n == 5 ? 42 : 0;
}

// ─── The ticket's exact shape: buf+i laundered through a pointer var ──────

static int use_add(int *p) {
    CLOBBER_FRAME();
    return chk(*p);
}
static int call_laundered_add(void) {
    int  buf[2] = {5, 0};
    int *q      = buf + 0; // pointer arithmetic on a frame-local array
    return use_add(q);     // q, not `buf + 0`, is the syntactic argument
}

static int use_sub(int *p) {
    CLOBBER_FRAME();
    return chk(*p);
}
static int call_laundered_sub(void) {
    int  buf[2] = {0, 5};
    int *q      = (buf + 1) - 0; // exercises ND_SUB too
    return use_sub(q);
}

// A second hop: buf -> q -> r, each an assignment through a plain pointer
// variable, so the escaping mark must propagate through more than one
// assignment (find_and_mark_escaping_addr's ND_ASSIGN -> rhs walk, applied
// twice: once to mark q's initializer, once more when q is re-escaped by
// being copied into r before r escapes).
static int use_chain(int *p) {
    CLOBBER_FRAME();
    return chk(*p);
}
static int call_laundered_chain(void) {
    int  buf[2] = {5, 0};
    int *q      = buf + 0;
    int *r      = q;
    return use_chain(r);
}

// ─── Positive control: laundered pointer arithmetic on a GLOBAL must stay
// correct AND keep TCO (no over-rejection from the new ND_ADD/ND_SUB case) ─

static int g_buf[2] = {5, 0};
static int use_global_add(int *p) {
    CLOBBER_FRAME();
    return chk(*p);
}
static int call_laundered_global(void) {
    int *q = g_buf + 0; // arithmetic on a global's base -- not frame-local
    return use_global_add(q);
}

// ─── Tests ──────────────────────────────────────────────────────────────

[[cccc::test]]
void test_718_laundered_pointer_add(void) {
    AssertEq(call_laundered_add(), 42);
}

[[cccc::test]]
void test_718_laundered_pointer_sub(void) {
    AssertEq(call_laundered_sub(), 42);
}

[[cccc::test]]
void test_718_laundered_pointer_chain(void) {
    AssertEq(call_laundered_chain(), 42);
}

[[cccc::test]]
void test_718_laundered_global_still_correct(void) {
    AssertEq(call_laundered_global(), 42);
}
