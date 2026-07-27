// Regression test for #775: F2I3/F2I3_F32 (the float->int conversion
// opcodes) implemented the cast as a bare host C cast
// "(long long)cccc_freg_get_f64(vm, rs)". For NaN, +-infinity, or a
// value out of range for long long, that cast is undefined behavior in
// the host compiler that built cccc -- the guest would observe whatever
// the host CPU's convert instruction happens to do (e.g. saturation on
// aarch64's FCVTZS vs LLONG_MIN on x86's cvttsd2si on overflow), and no
// FE_INVALID was ever raised as C23 Annex F requires.
//
// Fixed by defining the conversion as saturating (matching what aarch64
// already did, so the primary dev platform's behavior is unchanged) plus
// raising FE_INVALID on every special case, in cccc_f64_to_i64/
// cccc_f32_to_i64 (src/internal.h), used by op_F2I3_fn/op_F2I3_F32_fn
// (src/ops.c) and the compile-time constant-fold path in parse.c's
// eval2() (which had the identical UB in a bare "return eval_double(...)"
// implicitly truncating to int64_t).
//
// Extended for #780 (follow-up to #775): F2I3/F2I3_F32 carry no
// destination-signedness information, so they always saturate against the
// *signed* 64-bit range [-2^63, 2^63). A cast to an *unsigned* 64-bit
// destination -- well-defined in C for any finite value in [0, 2^64) --
// was getting clamped to LLONG_MAX instead of producing the correct
// unsigned result. Fixed with a dedicated F2U3/F2U3_F32 opcode pair that
// saturates against [0, 2^64) (cccc_f64_to_u64/cccc_f32_to_u64 in
// src/internal.h), selected by codegen whenever the cast destination is
// an unsigned 64-bit integer, plus the matching compile-time fold in
// parse.c's eval2()/eval_double().
#include <fenv.h>

// Global initializer: exercises the compile-time constant-fold path
// (eval2()) rather than the runtime F2U3 opcode.
unsigned long long g_u64_from_double = (unsigned long long)1.5e19;

int main(void) {
    // The ticket's motivating case: 1.5e19 is well within [0, 2^64) and
    // must convert exactly, not saturate to LLONG_MAX.
    if (g_u64_from_double != 15000000000000000000ULL) return 25;

    // In-function constant expression: also folds via eval2(), but through
    // a different call path (local initializer, not a global).
    unsigned long long local_const = (unsigned long long)1.5e19;
    if (local_const != 15000000000000000000ULL) return 26;

    volatile double u_in_range = 1.5e19;
    volatile double u_just_under_2p64 = 18446744073709549568.0; // < 2^64
    volatile double u_2p64 = 18446744073709551616.0;            // == 2^64
    volatile double u_neg_half = -0.5;
    volatile double u_neg_one = -1.0;

    // Runtime path (F2U3): 1.5e19 fits in [0, 2^64) and converts exactly,
    // raising nothing.
    feclearexcept(FE_ALL_EXCEPT);
    unsigned long long ur = (unsigned long long)u_in_range;
    if (ur != 15000000000000000000ULL) return 27;
    if (fetestexcept(FE_INVALID)) return 28;

    // Largest double strictly less than 2^64 converts exactly, raising
    // nothing -- the upper boundary must be compared against 2^64 itself,
    // not against "(double)ULLONG_MAX" (which rounds *up* to exactly 2^64
    // and would let this value slip through as "in range" incorrectly in
    // the other direction: a naive ">" guard here is fine, but a naive
    // "> (double)ULLONG_MAX" guard on u_2p64 below would not be).
    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)u_just_under_2p64;
    if (ur != 18446744073709549568ULL) return 29;
    if (fetestexcept(FE_INVALID)) return 30;

    // Exactly 2^64 is out of range for unsigned 64-bit -> saturates to
    // ULLONG_MAX with FE_INVALID raised.
    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)u_2p64;
    if (ur != 18446744073709551615ULL) return 31;
    if (!fetestexcept(FE_INVALID)) return 32;

    // +Inf and NaN saturate the same way as the signed case (to
    // ULLONG_MAX and 0 respectively), both raising FE_INVALID.
    volatile double u_pos_inf = 1.0 / 0.0;
    volatile double u_nan = 0.0 / 0.0;

    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)u_pos_inf;
    if (ur != 18446744073709551615ULL) return 33;
    if (!fetestexcept(FE_INVALID)) return 34;

    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)u_nan;
    if (ur != 0) return 35;
    if (!fetestexcept(FE_INVALID)) return 36;

    // -0.5 truncates toward zero to -0.0, which as an unsigned value is
    // simply 0 -- this is well-defined C and must raise NOTHING. This is
    // the boundary case that has no signed-conversion analogue: a naive
    // "x < 0" guard would wrongly flag this as invalid.
    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)u_neg_half;
    if (ur != 0) return 37;
    if (fetestexcept(FE_INVALID)) return 38;

    // -1.0 truncates to -1, which is genuinely out of range for unsigned
    // 64-bit -> saturates to 0 with FE_INVALID raised.
    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)u_neg_one;
    if (ur != 0) return 39;
    if (!fetestexcept(FE_INVALID)) return 40;

    // -Inf saturates to 0 with FE_INVALID raised, same as the signed case's
    // LLONG_MIN saturation direction.
    volatile double u_neg_inf = -1.0 / 0.0;
    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)u_neg_inf;
    if (ur != 0) return 41;
    if (!fetestexcept(FE_INVALID)) return 43; // 42 is this file's success code

    // float32 path (F2U3_F32): the source value is first rounded to
    // float precision, so the expected result is the *rounded* value
    // (15000000520515485696), not the original 1.5e19.
    volatile float uf_in_range = 1.5e19f;
    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)uf_in_range;
    if (ur != 15000000520515485696ULL) return 44;
    if (fetestexcept(FE_INVALID)) return 45;

    volatile float uf_neg_half = -0.5f;
    feclearexcept(FE_ALL_EXCEPT);
    ur = (unsigned long long)uf_neg_half;
    if (ur != 0) return 46;
    if (fetestexcept(FE_INVALID)) return 47;

    // Signed destinations must still behave exactly as #775 specified --
    // the new F2U3 gate must not change F2I3's existing behavior.
    volatile double nan_val = 0.0 / 0.0;
    volatile double pos_inf = 1.0 / 0.0;
    volatile double neg_inf = -1.0 / 0.0;
    volatile double too_big = 1e30;
    volatile double too_small = -1e30;
    volatile double exactly_2p63 = 9223372036854775808.0; // 2^63, out of range
    volatile double just_under_2p63 = 9223372036854774784.0; // largest double < 2^63
    volatile double in_range = 42.5;
    volatile double in_range_neg = -1.5;

    // NaN -> 0, with FE_INVALID raised.
    feclearexcept(FE_ALL_EXCEPT);
    long long r = (long long)nan_val;
    if (r != 0) return 1;
    if (!fetestexcept(FE_INVALID)) return 2;

    // +Inf -> LLONG_MAX, with FE_INVALID raised.
    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)pos_inf;
    if (r != 9223372036854775807LL) return 3;
    if (!fetestexcept(FE_INVALID)) return 4;

    // -Inf -> LLONG_MIN, with FE_INVALID raised.
    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)neg_inf;
    if (r != (-9223372036854775807LL - 1)) return 5;
    if (!fetestexcept(FE_INVALID)) return 6;

    // Out-of-range finite values saturate the same way.
    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)too_big;
    if (r != 9223372036854775807LL) return 7;
    if (!fetestexcept(FE_INVALID)) return 8;

    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)too_small;
    if (r != (-9223372036854775807LL - 1)) return 9;
    if (!fetestexcept(FE_INVALID)) return 10;

    // Exactly 2^63 is out of range for a signed 64-bit integer (max
    // representable is 2^63 - 1) -- this is the boundary that a naive
    // "x <= (double)LLONG_MAX" guard gets wrong, since LLONG_MAX itself
    // rounds up to exactly 2^63 when converted to double.
    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)exactly_2p63;
    if (r != 9223372036854775807LL) return 11;
    if (!fetestexcept(FE_INVALID)) return 12;

    // The largest double strictly less than 2^63 converts exactly and
    // raises nothing.
    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)just_under_2p63;
    if (r != 9223372036854774784LL) return 13;
    if (fetestexcept(FE_INVALID)) return 14;

    // Ordinary in-range values (positive and negative) convert exactly
    // and raise nothing.
    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)in_range;
    if (r != 42) return 15;
    if (fetestexcept(FE_INVALID)) return 16;

    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)in_range_neg;
    if (r != -1) return 17; // truncates toward zero, like a plain cast
    if (fetestexcept(FE_INVALID)) return 18;

    // Same special cases through the float32 path (F2I3_F32).
    volatile float fnan = 0.0f / 0.0f;
    volatile float fbig = 1e30f;
    volatile float fok = 7.5f;

    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)fnan;
    if (r != 0) return 19;
    if (!fetestexcept(FE_INVALID)) return 20;

    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)fbig;
    if (r != 9223372036854775807LL) return 21;
    if (!fetestexcept(FE_INVALID)) return 22;

    feclearexcept(FE_ALL_EXCEPT);
    r = (long long)fok;
    if (r != 7) return 23;
    if (fetestexcept(FE_INVALID)) return 24;

    return 42;
}
