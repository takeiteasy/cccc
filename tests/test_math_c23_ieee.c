// Regression test for #774: C23 IEC 60559:2020 interchange/classification
// functions (fmaximum/fminimum family, totalorder, canonicalize,
// getpayload/setpayload, llogb, fromfp/ufromfp, issignaling/iseqsig)
// were missing entirely. Implemented in software in src/stdlib/math.c
// (bit-pattern based, not FFI) since several are absent from Darwin's
// libm and glibc only gained fmaximum/fminimum in 2.35.
//
// (Previously avoided bare-exponent float literals like `1e10` here due
// to a separate tokenizer bug -- see test_float_literal_exponent.c for
// the regression test; fixed, so no longer a concern.)
//
// totalorder/totalordermag take pointers and fromfp/ufromfp/fromfpx/
// ufromfpx return intmax_t/uintmax_t -- both verified against the real
// glibc 2.39 declarations in bits/mathcalls.h rather than assumed, since
// Darwin's libm doesn't have these at all.
#include <math.h>
#include <fenv.h>

int main(void) {
    double nan_val = 0.0 / 0.0;

    // fmaximum/fminimum: propagate NaN (fmax/fmin instead prefer the
    // non-NaN operand).
    if (fmaximum(3.0, 5.0) != 5.0) return 1;
    if (fminimum(3.0, 5.0) != 3.0) return 2;
    if (!(fmaximum(3.0, nan_val) != fmaximum(3.0, nan_val))) return 3; // NaN
    if (!(fminimum(3.0, nan_val) != fminimum(3.0, nan_val))) return 4; // NaN

    // +0 > -0 for fmaximum/fminimum's zero rule. Distinguish -0.0 from
    // +0.0 via 1.0/x (-> -Inf vs +Inf) rather than signbit(), since
    // signbit(x) ((x) < 0) is itself a separate pre-existing bug (see
    // include/math.h and the ticket filed for it) that would give a false
    // negative here regardless of whether fmaximum/fminimum are correct.
    if (!(fmaximum(0.0, -0.0) == 0.0 && 1.0 / fmaximum(0.0, -0.0) > 0.0)) return 5;
    if (!(fminimum(0.0, -0.0) == 0.0 && 1.0 / fminimum(0.0, -0.0) < 0.0)) return 6;

    // fmaximum_num/fminimum_num: ignore a single NaN like fmax/fmin.
    if (fmaximum_num(3.0, nan_val) != 3.0) return 7;
    if (fminimum_num(3.0, nan_val) != 3.0) return 8;
    if (!(fmaximum_num(nan_val, nan_val) != fmaximum_num(nan_val, nan_val))) return 9;

    // fmaximum_mag/fminimum_mag: compare by magnitude.
    if (fmaximum_mag(-5.0, 3.0) != -5.0) return 10;
    if (fminimum_mag(-5.0, 3.0) != 3.0) return 11;

    float fx = fmaximumf(3.0f, 5.0f);
    if (fx != 5.0f) return 12;

    // totalorder: -0 precedes +0; -1 precedes +1; not symmetric. Takes
    // pointers (matching glibc/ISO C), needed to observe a signaling
    // NaN's exact bit pattern without an intervening FP op quieting it.
    double tneg0 = -0.0, tpos0 = 0.0, tnegone = -1.0, tposone = 1.0;
    if (!totalorder(&tneg0, &tpos0)) return 13;
    if (totalorder(&tpos0, &tneg0)) return 14;
    if (!totalorder(&tnegone, &tposone)) return 15;
    if (totalorder(&tposone, &tnegone)) return 16;
    float ftnegone = -1.0f, ftposone = 1.0f;
    if (!totalorderf(&ftnegone, &ftposone)) return 17;

    // totalordermag: compares |x|, |y|.
    double tm5 = -5.0, tm3 = 3.0, tm3b = -3.0, tm5b = 5.0;
    if (totalordermag(&tm5, &tm3)) return 18;   // |-5| > |3| -> false
    if (!totalordermag(&tm3b, &tm5b)) return 19; // |-3| < |5| -> true

    // canonicalize: IEEE 754 binary formats have no non-canonical
    // encodings, so this is a copy that always succeeds.
    double cx, src = 3.5;
    if (canonicalize(&cx, &src) != 0) return 20;
    if (cx != 3.5) return 21;

    // getpayload/setpayload/setpayload_sig round-trip.
    double p = getpayload(&nan_val);
    if (p != 0.0) return 22; // a plain 0.0/0.0 NaN has payload 0

    double non_nan = 1.5;
    if (getpayload(&non_nan) != -1.0) return 23; // not a NaN -> -1

    double sp;
    if (setpayload(&sp, 42.0) != 0) return 24;
    if (!(sp != sp)) return 25; // must be NaN
    if (getpayload(&sp) != 42.0) return 26;

    double ssig;
    if (setpayloadsig(&ssig, 7.0) != 0) return 27;
    if (!issignaling(ssig)) return 28;
    if (setpayloadsig(&ssig, 0.0) == 0) return 29; // payload 0 -> would be Inf, must fail

    // llogb: like ilogb but returns long.
    if (llogb(8.0) != 3) return 30;
    if (llogbf(8.0f) != 3) return 31;

    // fromfp/ufromfp: round to fit a width-bit integer, returned as
    // intmax_t/uintmax_t (NOT the source floating type -- these
    // generalize lround/llround with configurable rounding + width,
    // matching glibc's real intmax_t fromfp(double, int, unsigned int)
    // signature). 0 (+ FE_INVALID) if it doesn't fit.
    if (fromfp(3.7, FP_INT_TONEAREST, 8) != 4) return 32;
    if (fromfp(10000000000.0, FP_INT_TONEAREST, 8) != 0) return 33; // overflow
    if (ufromfp(3.7, FP_INT_UPWARD, 8) != 4) return 34;
    if (ufromfp(-1.0, FP_INT_TONEAREST, 8) != 0) return 35; // negative -> invalid

    feclearexcept(FE_ALL_EXCEPT);
    (void)fromfp(10000000000.0, FP_INT_TONEAREST, 8);
    if (!fetestexcept(FE_INVALID)) return 36;

    feclearexcept(FE_ALL_EXCEPT);
    intmax_t fx2 = fromfpx(3.7, FP_INT_TONEAREST, 16);
    if (fx2 != 4) return 37;
    if (!fetestexcept(FE_INEXACT)) return 38; // 'x' variant: rounded != input

    // issignaling/iseqsig/iscanonical.
    if (issignaling(nan_val)) return 39; // 0.0/0.0 is a quiet NaN
    if (!iscanonical(1.0)) return 40;

    feclearexcept(FE_ALL_EXCEPT);
    if (iseqsig(ssig, ssig)) return 41; // sNaN != itself
    if (!fetestexcept(FE_INVALID)) return 51; // signaling NaN operand -> FE_INVALID
    if (!iseqsig(1.0, 1.0)) return 52;

    return 42;
}
