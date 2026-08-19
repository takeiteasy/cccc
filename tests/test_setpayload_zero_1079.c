// Regression test for #1079: cccc_setpayload_impl/cccc_setpayloadf_impl
// (src/stdlib/math.c) used to leave the destination completely untouched
// on a failed setpayload/setpayloadsig call. Real glibc -- and the
// documented setpayload(3) contract -- instead zero the destination when
// the payload wasn't installed ("if the payload was not successfully
// installed, zero is stored in *cx"). Covers all six entry points
// (setpayload/f/l, setpayloadsig/f/l) and every distinct failure reason
// (negative payload, non-integral payload, out-of-range payload, and a
// zero payload for the signaling form, which would otherwise be +Inf).
#include <math.h>

int main(void) {
    // setpayload: negative payload.
    double d = 99.0;
    if (setpayload(&d, -1.0) == 0) return 1;
    if (d != 0.0) return 2;

    // setpayload: non-integral payload.
    d = 99.0;
    if (setpayload(&d, 1.5) == 0) return 3;
    if (d != 0.0) return 4;

    // setpayload: out-of-range payload (> 51-bit payload max).
    d = 99.0;
    if (setpayload(&d, 1.0e20) == 0) return 5;
    if (d != 0.0) return 6;

    // setpayloadf: same three reasons, float destination (4-byte write).
    float f = 99.0f;
    if (setpayloadf(&f, -1.0f) == 0) return 7;
    if (f != 0.0f) return 8;

    f = 99.0f;
    if (setpayloadf(&f, 1.5f) == 0) return 9;
    if (f != 0.0f) return 10;

    f = 99.0f;
    if (setpayloadf(&f, 1.0e20f) == 0) return 11;
    if (f != 0.0f) return 12;

    // setpayloadl forwards to the double impl.
    double ld = 99.0;
    if (setpayloadl(&ld, -1.0) == 0) return 13;
    if (ld != 0.0) return 14;

    // setpayloadsig/setpayloadsigf/setpayloadsigl: zero payload would be
    // +Inf, not a signaling NaN -- must fail and zero the destination too.
    d = 99.0;
    if (setpayloadsig(&d, 0.0) == 0) return 15;
    if (d != 0.0) return 16;

    f = 99.0f;
    if (setpayloadsigf(&f, 0.0f) == 0) return 17;
    if (f != 0.0f) return 18;

    ld = 99.0;
    if (setpayloadsigl(&ld, 0.0) == 0) return 19;
    if (ld != 0.0) return 20;

    // Success-path control: destination must be the expected NaN, not
    // zeroed, when the call actually succeeds.
    d = 0.0;
    if (setpayload(&d, 42.0) != 0) return 21;
    if (!(d != d)) return 22; // must be NaN, not 0.0

    return 42;
}
