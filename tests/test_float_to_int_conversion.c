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
#include <fenv.h>

int main(void) {
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
