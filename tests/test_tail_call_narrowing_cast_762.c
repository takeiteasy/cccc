// CCCC_FLAGS: --testing -O2
//
// The VM eliminates eligible tail calls unconditionally, so the VM leg
// ignores the -O2 above; it is retained for the -c=native round-trip leg,
// where -O<n> is forwarded verbatim to the host cc (#1159) and
// test_762_identity_cast_tail_recursion_still_tco's 1M-deep recursion needs
// an optimising host -O to not overflow the native stack. -O2 (not -O1) so
// both gcc-16 and clang eliminate the call -- gcc's -O1 heuristic does not.
//
// #762: the parser always wraps a `return` expression in ND_CAST, even for
// identity conversions. To reach the underlying ND_FUNCALL for
// can_emit_tail_call, ND_RETURN codegen used to strip through *every*
// ND_CAST wrapper unconditionally, then emit only the un-cast funcall as the
// CALLT's evaluated expression -- silently dropping any conversion those
// casts encoded.
//
//   static int g(unsigned char y) { return (int)y * 1000 + 37; }
//   static int f(int x) { return (unsigned char) g((unsigned char)x); }
//
// f(200): g(200) == 200037, and (unsigned char)200037 == 101. Before the
// fix, `return (unsigned char) g(...)` in tail position became a bare CALLT
// to g, handing g's raw 200037 straight back to f's own caller instead of
// the truncated 101.
//
// The fix (return_repr_key/cast_is_repr_noop in codegen.c, ~line 1350) only
// strips a return-site cast when it is a representation no-op on a value
// already normalised to the source type -- see man/VM.md's tail-call
// Eligibility list.

#include <stddef.h>

// ─── (1) Ticket repro: unsigned char narrowing of an int-returning call ───

// noinline: this test targets the CALLT path specifically; an inlined callee
// takes the separate expr_already_eval fall-through covered by case (6).
__attribute__((noinline)) static int g_uchar(unsigned char y) {
    return (int)y * 1000 + 37;
}
static int f_uchar(int x) {
    return (unsigned char)g_uchar((unsigned char)x);
}

// ─── (2) short narrowing, value above 2^16 ─────────────────────────────────

__attribute__((noinline)) static int g_short(int x) {
    return x;
}
static short f_short(int x) {
    return (short)g_short(x);
}

// ─── (3) _Bool narrowing of a nonzero, non-one int ─────────────────────────

__attribute__((noinline)) static int g_bool(int x) {
    return x;
}
static _Bool f_bool(int x) {
    return (_Bool)g_bool(x);
}

// ─── (4) int narrowing of a long-returning call, value above 2^32 ─────────

__attribute__((noinline)) static long g_long(long x) {
    return x;
}
static int f_int_of_long(long x) {
    return (int)g_long(x);
}

// ─── (5) float rounding of a double-returning call ─────────────────────────

__attribute__((noinline)) static double g_double(double x) {
    return x;
}
static float f_float_of_double(double x) {
    return (float)g_double(x);
}

// ─── (6) Inlined-callee variant of (1): expr_already_eval fall-through ────

static int g_uchar_inl(unsigned char y) {
    return (int)y * 1000 + 37;
}
static int f_uchar_inl(int x) {
    return (unsigned char)g_uchar_inl((unsigned char)x);
}

// ─── (7) Positive control: ordinary identity-cast tail recursion must keep
//         TCO (a lost CALLT here is a stack-depth regression, not a wrong
//         value -- this deep a recursion would blow the host stack without
//         TCO). ──────────────────────────────────────────────────────────

static int f_tail_recur(int n) {
    return n ? f_tail_recur(n - 1) : 0;
}

// ─── Tests ──────────────────────────────────────────────────────────────

[[cccc::test]]
void test_762_uchar_narrowing_return(void) {
    AssertEq(f_uchar(200), 101);
}

[[cccc::test]]
void test_762_short_narrowing_return(void) {
    AssertEq(f_short(70000), (short)70000);
}

[[cccc::test]]
void test_762_bool_narrowing_return(void) {
    AssertEq(f_bool(5), 1);
}

[[cccc::test]]
void test_762_int_narrowing_of_long_return(void) {
    long v = (1LL << 33) + 7;
    AssertEq(f_int_of_long(v), (int)v);
}

[[cccc::test]]
void test_762_float_rounding_of_double_return(void) {
    // 2^24+1 is exactly representable as a double but not as a float (24-bit
    // mantissa) -- rounding moves it by >= 1.0, far outside AssertFloatEq's
    // default 1e-6 tolerance, so a dropped emit_fround_f32 is guaranteed to
    // be caught (a value near 1/3 would round within tolerance and miss it).
    double d = 16777217.0;
    AssertFloatEq((float)d, f_float_of_double(d));
}

[[cccc::test]]
void test_762_uchar_narrowing_inlined_callee(void) {
    AssertEq(f_uchar_inl(200), 101);
}

[[cccc::test]]
void test_762_identity_cast_tail_recursion_still_tco(void) {
    AssertEq(f_tail_recur(1000000), 0);
}
