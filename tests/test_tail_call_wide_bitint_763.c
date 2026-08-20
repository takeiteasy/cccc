// CCCC_FLAGS: --testing -O1
// CCCC_MATRIX_SKIP: exercises can_emit_tail_call's CALLT eligibility, which
// requires -O1; the per-pass matrix forces -O0 and would trivially pass
// without exercising the guard this test locks in.
//
// #763: can_emit_tail_call (src/codegen.c) already rejects a tail call whose
// return type is a struct, union, or vector, because those returns go
// through a frame-relative RETBUF/scratch-slot buffer that CALLT's frame
// reuse would invalidate (same hazard class as #714/#716/#718). A wide
// _BitInt(N>64) return is materialised the same way -- as an address into a
// frame-local scratch buffer (alloc_wide_bitint_temp) -- but wasn't covered
// by that check.
//
// Investigation found the hazard was not actually reachable: the ND_RETURN
// cast-stripping loop (guarded by #762's return_repr_key/cast_is_repr_noop)
// already never strips the mandatory return-site ND_CAST for a wide-BitInt
// type (its key is always -1, matching nothing), so can_emit_tail_call never
// even sees a bare ND_FUNCALL for one of these calls -- it was blocked by
// accident, not by design. This test locks in an explicit, direct guard in
// can_emit_tail_call (`is_wide_bitint(expr->ty)` alongside the existing
// struct/union/vector check) so the invariant no longer depends on that
// coincidence, plus a correctness regression test for values that don't fit
// in 64 bits (a scratch-buffer clobber from a hypothetical future CALLT
// would show up as a wrong low/high word here, not a crash).

#include <stddef.h>

// A value that needs more than 64 bits: 0x1_0000_0000_0000_0007 (low word 7,
// high word 1). Fits in _BitInt(128).
__attribute__((noinline)) static _BitInt(128) g_wide(_BitInt(128) y) {
    return y + 1;
}

static _BitInt(128) f_wide_tail(_BitInt(128) x) {
    return g_wide(x); // tail position -- must NOT become CALLT
}

// Self-recursive tail call, wide-BitInt accumulator. Bounded depth: this is
// intentionally NOT deep enough to require TCO (CALLT is correctly never
// used here per #763's explicit guard) -- it only needs to prove ordinary
// CALL+LEV3 recursion still produces the right wide-BitInt result.
static _BitInt(128) f_wide_recur(_BitInt(128) n, _BitInt(128) acc) {
    if (n <= 0)
        return acc;
    return f_wide_recur(n - 1, acc + n);
}

[[cccc::test]]
void test_763_wide_bitint_tail_call_correct(void) {
    _BitInt(128) start = ((_BitInt(128))1 << 64) + 6; // 0x1_0000_0000_0000_0006
    _BitInt(128) result   = f_wide_tail(start);
    _BitInt(128) expected = ((_BitInt(128))1 << 64) + 7;
    unsigned long long lo =
        (unsigned long long)(result & 0xFFFFFFFFFFFFFFFFULL);
    unsigned long long hi =
        (unsigned long long)((result >> 64) & 0xFFFFFFFFFFFFFFFFULL);
    unsigned long long elo =
        (unsigned long long)(expected & 0xFFFFFFFFFFFFFFFFULL);
    unsigned long long ehi =
        (unsigned long long)((expected >> 64) & 0xFFFFFFFFFFFFFFFFULL);
    AssertEq(lo, elo);
    AssertEq(hi, ehi);
}

[[cccc::test]]
void test_763_wide_bitint_tail_recursion_correct(void) {
    _BitInt(128) result = f_wide_recur(1000, 0);
    // sum 1..1000 == 500500, well within 64 bits -- only the low word matters.
    unsigned long long lo =
        (unsigned long long)(result & 0xFFFFFFFFFFFFFFFFULL);
    AssertEq(lo, 500500ULL);
}
