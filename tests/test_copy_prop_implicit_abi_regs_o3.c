// CCCC_FLAGS: --optimize=3
// CCCC_MATRIX_SKIP: depends on --optimize=3 (copy-prop specifically)
#include <stdio.h>
// Ticket #760: copy-prop sub-pass B (dead-MOV3 elimination, src/optimize.c)
// modeled the implicit ABI-register reads of MCPY/MALC/REALC/SETJMP/etc.,
// but IOVFL, MSET, and VRAISE fell through to `default: break` with no
// modeling at all. Their implicit reads of A0/A1/A2 were never
// MARK_INT_USE'd, so a live MOV3 feeding one of those registers immediately
// before the opcode could be judged dead and NOP'd by a later redefinition
// of the same register -- deleting the argument setup itself, not just a
// downstream use.
//
// IOVFL reproduced live (pre-fix): two `__builtin_add_overflow` checks back
// to back in the same straight-line block (no branch/call between them, so
// sub-pass B's basic-block tracking never resets) each feed IOVFL's `b`
// argument via `MOV3 A1, <promoted t>`. The second MOV3 A1 write made
// sub-pass B judge the first one dead (IOVFL's read was invisible), NOPing
// it -- the first IOVFL then read A1's stale prior content instead of `t`.
// Under `--optimize=3` this returned garbage (e.g. 993984880) instead of 156.
//
// MSET and VRAISE are ALSO added to the shared op_implicit_abi_regs() table
// this ticket introduces (src/optimize.c), but neither has a demonstrated
// live repro: MSET's sole emission site (codegen.c, ND_MEMZERO) always
// feeds A0 via LEA3 and A2 via a compile-time-constant LI3, both of which
// the generic size>=2 decode already tracks and kills correctly regardless
// of MSET's own read -- a MOV3 never reaches those registers immediately
// before MSET. VRAISE's sole emission site (codegen.c, raise() lowering)
// always delivers its `sig` argument via `SX4 A0, <preg>`, never a bare
// MOV3, across every construction tried. Both entries are still correct and
// necessary (documented invariant + required for the opt_elim_ext fix in
// the companion #761 test), just not reachable as a live miscompile under
// today's codegen. See docs/OPTIMIZATION.md for the invariant.
static int overflow_pair(int sum, int t) {
    int r1, r2;
    int of1 = __builtin_add_overflow(sum, t, &r1);
    int of2 = __builtin_add_overflow(sum, t, &r2);
    (void)of1; (void)of2;
    return r1 + r2;
}

int main(void) {
    int total = 0;
    int junk = 0;
    for (int i = 0; i < 5; i++) {
        int t = i * 3;
        int r1, r2;
        int of1 = __builtin_add_overflow(total, t, &r1);
        int of2 = __builtin_add_overflow(total, t, &r2);
        junk = junk + of1 + of2;
        total = r1 + r2;
    }
    int v = total + junk;
    if (v != 156) {
        printf("overflow_pair loop: got %d, want 156\n", v);
        return 1;
    }
    if (overflow_pair(3, 4) != 14) {
        printf("overflow_pair(3,4): got %d, want 14\n", overflow_pair(3, 4));
        return 2;
    }
    return 42;
}
