// CCCC_FLAGS: --testing -O1
// CCCC_MATRIX_SKIP: exercises CALLT tail-call codegen, which requires -O1;
// the per-pass matrix forces -O0 and would trivially pass without exercising
// the bug this test guards against.
//
// #716: CALLT reuses the caller's frame (op_CALLT_fn does `sp = bp`), so a
// tail call that hands the callee a pointer into the caller's own frame
// dangles the moment the callee's prologue/body overwrites that stack slot.
//
//   int use(double *p){ printf(...); return chk(5, *p); }
//   int main(void){ double x=4.5; return use(&x); }   // main -> use is a tail call
//
// Before the fix, can_emit_tail_call() didn't know an argument could carry
// the address of a caller local, so `main`'s call into `use` became a
// CALLT, `use`'s own frame overwrote the slot `x` lived in, and `*p` read
// garbage. See can_emit_tail_call / tail_arg_carries_frame_addr in
// codegen.c and docs/VM.md's tail-call Eligibility list.
//
// Each helper below is address-precise: `use_val` takes a *value* (safe,
// keeps TCO), while the `use_*` variants that receive a frame-local address
// must have TCO suppressed. The global/pure-recursion cases are positive
// controls that must both stay correct AND keep their tail call.

#include <stddef.h>

// Overwrite the top of the current frame with known-bad values. Inlined
// directly into each unsafe `use_*` helper below (not a nested call — a
// nested call pushes its own frame *below* the reused one and doesn't
// reliably overlap the slot the caller's escaped local occupied), so that
// when CALLT has reused the caller's frame for this callee, the write lands
// on the exact memory the caller's local used to occupy. This makes a
// dangling pointer to it deterministically read garbage instead of merely
// risking it — without this, a trivial callee body might not happen to
// touch that slot (verified empirically: e.g. `buf+i`'s slot is not
// necessarily touched by a *sibling* call's own frame), and the regression
// test could then pass even without the fix.
static volatile long g_sink;
#define CLOBBER_FRAME()                                                      \
    do {                                                                     \
        volatile long junk[8];                                               \
        for (int _i = 0; _i < 8; _i++)                                       \
            junk[_i] = 0xdeaddead + _i;                                      \
        g_sink = junk[0]; /* keep the writes from being optimized away */    \
    } while (0)

// ─── Route (a): explicit &local as a tail-call argument ───────────────────

static int chk_d(int n, double d) { return (n == 5 && d == 4.5) ? 42 : 0; }
static int use_addr_d(double *p) { CLOBBER_FRAME(); return chk_d(5, *p); }
static int call_use_addr_d(void) {
    double x = 4.5;
    return use_addr_d(&x); // tail call carries &x — must not become CALLT
}

static int chk_i(int n) { return n == 5 ? 42 : 0; }
static int use_addr_i(int *p) { CLOBBER_FRAME(); return chk_i(*p); }
static int call_use_addr_i(void) {
    int x = 5;
    return use_addr_i(&x);
}

// ─── Pointer arithmetic off a frame-local array ────────────────────────────

static int use_arith(int *p) { CLOBBER_FRAME(); return chk_i(*p); }
static int call_use_arith(void) {
    int buf[2] = {5, 0};
    int i = 0;
    return use_arith(buf + i); // buf+i is a frame-local address, not marked
                                // by mark_addr_escapes -- must still be caught
}

// ─── Route (b): frame address laundered through a pointer variable ────────

static int use_ptrvar(int *p) { CLOBBER_FRAME(); return chk_i(*p); }
static int call_use_ptrvar(void) {
    int x = 5;
    int *q = &x; // &x escapes via assignment to a pointer lvalue
    return use_ptrvar(q); // q, not &x, is the syntactic argument
}

// ─── Positive controls: must stay correct AND keep TCO ─────────────────────

static double g_x = 4.5;
static int use_global(double *p) { CLOBBER_FRAME(); return chk_d(5, *p); }
static int call_use_global(void) {
    // Address of a global, not a frame-local: safe even though CALLT still
    // reuses the frame and use_global still clobbers it, because g_x never
    // lived in that frame to begin with.
    return use_global(&g_x);
}

static int use_val(double d) { return chk_d(5, d); }
static int call_use_val(void) {
    double x = 4.5;
    return use_val(x); // value, not address — safe regardless of TCO
}

static long tail_sum(long n, long acc) {
    if (n <= 0)
        return acc;
    return tail_sum(n - 1, acc + n); // pure-value tail recursion — must keep TCO
}

// ─── Tests ──────────────────────────────────────────────────────────────

[[cccc::test]]
void test_716_addr_of_local_double_arg(void) {
    AssertEq(call_use_addr_d(), 42);
}

[[cccc::test]]
void test_716_addr_of_local_int_arg(void) {
    AssertEq(call_use_addr_i(), 42);
}

[[cccc::test]]
void test_716_pointer_arith_on_local_array(void) {
    AssertEq(call_use_arith(), 42);
}

[[cccc::test]]
void test_716_addr_laundered_through_pointer_var(void) {
    AssertEq(call_use_ptrvar(), 42);
}

[[cccc::test]]
void test_716_global_addr_tail_call_still_correct(void) {
    AssertEq(call_use_global(), 42);
}

[[cccc::test]]
void test_716_value_arg_tail_call_still_correct(void) {
    AssertEq(call_use_val(), 42);
}

[[cccc::test]]
void test_716_pure_recursion_still_correct(void) {
    AssertEq(tail_sum(500000, 0), 500000LL * 500001 / 2);
}
