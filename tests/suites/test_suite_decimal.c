// CCCC_FLAGS: --testing
// C23 _Decimal32/64/128 (tracker #402): real IEEE-754-2008 decimal
// arithmetic via the Intel BID library, opt-in with CCCC_HAS_DECIMAL=1.
//
// test_decimal_sizes runs unconditionally (declarations/sizeof/struct
// layout/alignment are always real, regardless of CCCC_HAS_DECIMAL). Every
// other test in this file is guarded by __STDC_IEC_60559_DFP__ and is a
// no-op in the default (decimal-off) build -- see test_suite_c23.c's
// test_c23_decimal for the basic smoke test that runs in both builds.
//
// See docs/COVERAGE.md and docs/VM.md for the opcode/ABI design notes.

#include <float.h>
#include <string.h>

[[cccc::test(return = 42)]]
int test_decimal_sizes(void) {
    if (sizeof(_Decimal32) != 4)   return 1;
    if (sizeof(_Decimal64) != 8)   return 2;
    if (sizeof(_Decimal128) != 16) return 3;
    if (_Alignof(_Decimal32) != 4)   return 4;
    if (_Alignof(_Decimal64) != 8)   return 5;
    if (_Alignof(_Decimal128) != 16) return 6;

    struct S32  { char c; _Decimal32  d; };
    struct S64  { char c; _Decimal64  d; };
    struct S128 { char c; _Decimal128 d; };
    if (sizeof(struct S32)  != 8)  return 10;
    if (sizeof(struct S64)  != 16) return 11;
    if (sizeof(struct S128) != 32) return 12;

    _Decimal64 arr[4];
    if (sizeof(arr) != 32) return 13;

    return 42;
}

#ifdef __STDC_IEC_60559_DFP__

[[cccc::test(return = 42)]]
int test_decimal_arithmetic_d32(void) {
    _Decimal32 a = 1.1df, b = 2.2df;
    if (a + b != 3.3df) return 1;
    if (a - b != -1.1df) return 2;
    _Decimal32 c = 5.df, d = 2.df;
    if (c * d != 10.df) return 3;
    if (c / d != 2.5df) return 4;
    if (-c != -5.df) return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_arithmetic_d64(void) {
    _Decimal64 a = 1.1dd, b = 2.2dd;
    if (a + b != 3.3dd) return 1;
    if (0.1dd + 0.2dd != 0.3dd) return 2; // exact under BID; false for binary FP
    _Decimal64 c = 5.dd, d = 2.dd;
    if (c - d != 3.dd) return 3;
    if (c * d != 10.dd) return 4;
    if (c / d != 2.5dd) return 5;
    if (-c != -5.dd) return 6;
    // nested expressions must not clobber earlier operands (regression
    // check for the register-lifetime bug found while implementing #402)
    _Decimal64 w = 1.dd, x = 2.dd, y = 3.dd, z = 4.dd;
    if ((w + x) + (y + z) != 10.dd) return 7;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_arithmetic_d128(void) {
    _Decimal128 a = 1.1dl, b = 2.2dl;
    if (a + b != 3.3dl) return 1;
    if (a - b != -1.1dl) return 2;
    _Decimal128 c = 5.dl, d = 2.dl;
    if (c * d != 10.dl) return 3;
    if (c / d != 2.5dl) return 4;
    if (-c != -5.dl) return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_comparisons(void) {
    _Decimal64 a = 1.dd, b = 2.dd;
    if (!(a < b))  return 1;
    if (a > b)     return 2;
    if (!(a <= b)) return 3;
    if (a >= b)    return 4;
    if (!(a != b)) return 5;
    if (a == b)    return 6;
    if (!(a == a)) return 7;
    if (a != a)    return 8;
    // quantum preservation: equal value, different quantum, still equal
    _Decimal64 x = 1.0dd, y = 1.00dd;
    if (x != y) return 9;
    // -0 == +0
    _Decimal64 negz = -0.dd, posz = 0.dd;
    if (negz != posz) return 10;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_conversions(void) {
    // int <-> decimal
    _Decimal64 d = (_Decimal64)7;
    if (d != 7.dd) return 1;
    if ((int)d != 7) return 2;
    if ((int)3.9dd != 3) return 3;   // truncating, not rounding
    if ((int)(-3.9dd) != -3) return 4;
    unsigned u = (unsigned)5.dd;
    if (u != 5u) return 5;

    // binary float <-> decimal
    double bin = (double)2.5dd;
    if (bin != 2.5) return 6;
    _Decimal64 fromf = (_Decimal64)0.5;
    if (fromf != 0.5dd) return 7;
    float f = (float)1.25dd;
    if (f != 1.25f) return 8;

    // decimal <-> decimal (widening/narrowing)
    _Decimal32 s = 3.5df;
    _Decimal64 s64 = (_Decimal64)s;
    if (s64 != 3.5dd) return 9;
    _Decimal128 s128 = (_Decimal128)s64;
    if ((_Decimal64)s128 != s64) return 10;
    _Decimal32 back = (_Decimal32)s128;
    if (back != s) return 11;

    // _Decimal128 <-> long long at a precision boundary a 64-bit int
    // fully covers (34 significant decimal digits comfortably holds any
    // int64_t exactly).
    long long big = 123456789012345LL;
    _Decimal128 dbig = (_Decimal128)big;
    if ((long long)dbig != big) return 12;

    return 42;
}

// _Decimal128 by-value argument and return (the highest-risk ABI path --
// #714's struct-ABI reuse for a scratch-slot-passed, RETBUF-returned value).
static _Decimal128 add128(_Decimal128 a, _Decimal128 b) {
    return a + b;
}

[[cccc::test(return = 42)]]
int test_decimal128_byvalue_abi(void) {
    _Decimal128 a = 1.1dl, b = 2.2dl;
    _Decimal128 r = add128(a, b);
    if (r != 3.3dl) return 1;
    // arguments are copies: mutating inside the callee must not affect the
    // caller's originals
    if (a != 1.1dl) return 2;
    if (b != 2.2dl) return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_struct_array(void) {
    struct Money { char currency[4]; _Decimal64 amount; };
    struct Money m = {"USD", 19.99dd};
    if (m.amount != 19.99dd) return 1;

    struct Nested { struct Money m; int flag; };
    struct Nested n = {{"GBP", 5.dd}, 1};
    if (n.m.amount != 5.dd) return 2;

    _Decimal64 prices[3] = {1.dd, 2.dd, 3.dd};
    if (prices[0] + prices[1] + prices[2] != 6.dd) return 3;

    static _Decimal64 g_price = 42.dd;
    if (g_price != 42.dd) return 4;

    static _Decimal64 g_uninit;
    if (g_uninit != 0.dd) return 5; // all-zero bytes must compare equal to 0

    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_to_chars(void) {
    char buf[64];
    __builtin_decimal_to_chars(buf, sizeof buf, 3.3dd);
    if (__builtin_strcmp(buf, "3.3") != 0) return 1;
    __builtin_decimal_to_chars(buf, sizeof buf, -1.5dd);
    if (__builtin_strcmp(buf, "-1.5") != 0) return 2;
    __builtin_decimal_to_chars(buf, sizeof buf, 0.dd);
    if (__builtin_strcmp(buf, "0") != 0) return 3;
    int n = __builtin_decimal_to_chars(buf, sizeof buf, 100.dd);
    if (n != 3 || __builtin_strcmp(buf, "100") != 0) return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_float_h(void) {
    char buf[512];
    __builtin_decimal_to_chars(buf, sizeof buf, DEC64_EPSILON);
    if (__builtin_strcmp(buf, "0.000000000000001") != 0) return 1;
    if (DEC32_MANT_DIG != 7)   return 2;
    if (DEC64_MANT_DIG != 16)  return 3;
    if (DEC128_MANT_DIG != 34) return 4;
    // round-trip through the macro rather than asserting exact digit counts
    // for MAX/TRUE_MIN, whose magnitudes are enormous when fully expanded
    int n = __builtin_decimal_to_chars(buf, sizeof buf, DEC64_MAX);
    if (n <= 0) return 5;
    n = __builtin_decimal_to_chars(buf, sizeof buf, DEC64_TRUE_MIN);
    if (n <= 0) return 6;
    return 42;
}

// -- <math.h> transcendentals (tracker #828, phase 2 of #402) --------------
// bid{32,64,128}_{sqrt,exp,log,pow,sin,cos,...} exposed via
// include/decimal_math.h + src/stdlib/decimal_math.c. Exact-string
// assertions (__builtin_decimal_to_chars + strcmp) are used only for
// exactly-representable results; transcendentals use a decimal tolerance
// compare since BID's digit count drives the string form.

#include <math.h>

static int approx_eqd64(_Decimal64 got, _Decimal64 want, _Decimal64 tol) {
    _Decimal64 diff = got - want;
    if (diff < 0.dd) diff = -diff;
    return diff < tol;
}

[[cccc::test(return = 42)]]
int test_decimal_math_algebraic(void) {
    if (sqrtd32(9.df) != 3.df) return 1;
    if (sqrtd64(4.dd) != 2.dd) return 2;
    if (sqrtd128(25.dl) != 5.dl) return 3;
    if (cbrtd64(27.dd) != 3.dd) return 4;
    if (hypotd64(3.dd, 4.dd) != 5.dd) return 5;
    if (fmad64(2.dd, 3.dd, 4.dd) != 10.dd) return 6;
    if (fmad128(2.dl, 3.dl, 4.dl) != 10.dl) return 7;
    if (fabsd64(-5.dd) != 5.dd) return 8;
    if (fabsd128(-5.dl) != 5.dl) return 9;
    if (fdimd64(5.dd, 3.dd) != 2.dd) return 10;
    if (fdimd64(3.dd, 5.dd) != 0.dd) return 11;
    if (fmind64(3.dd, 5.dd) != 3.dd) return 12;
    if (fmaxd64(3.dd, 5.dd) != 5.dd) return 13;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_exp_log(void) {
    if (expd64(0.dd) != 1.dd) return 1;
    if (logd64(1.dd) != 0.dd) return 2;
    if (log10d64(100.dd) != 2.dd) return 3;
    if (log2d64(8.dd) != 3.dd) return 4;
    if (exp2d64(3.dd) != 8.dd) return 5;
    if (exp10d64(2.dd) != 100.dd) return 6;
    if (powd64(2.dd, 10.dd) != 1024.dd) return 7;
    if (powd32(2.df, 10.df) != 1024.df) return 8;
    if (powd128(2.dl, 10.dl) != 1024.dl) return 9;
    if (!approx_eqd64(expd64(1.dd), 2.71828182845905dd, 1e-10dd)) return 10;
    if (!approx_eqd64(expm1d64(0.dd), 0.dd, 1e-15dd)) return 11;
    if (!approx_eqd64(log1pd64(0.dd), 0.dd, 1e-15dd)) return 12;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_trig(void) {
    if (sind64(0.dd) != 0.dd) return 1;
    if (cosd64(0.dd) != 1.dd) return 2;
    if (tand64(0.dd) != 0.dd) return 3;
    if (asind64(0.dd) != 0.dd) return 4;
    if (atand64(0.dd) != 0.dd) return 5;
    if (atan2d64(0.dd, 1.dd) != 0.dd) return 6;
    if (!approx_eqd64(sind64(1.dd), 0.841470984807897dd, 1e-9dd)) return 7;
    if (!approx_eqd64(cosd64(1.dd), 0.540302305868140dd, 1e-9dd)) return 8;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_hyperbolic(void) {
    if (sinhd64(0.dd) != 0.dd) return 1;
    if (coshd64(0.dd) != 1.dd) return 2;
    if (tanhd64(0.dd) != 0.dd) return 3;
    if (asinhd64(0.dd) != 0.dd) return 4;
    if (atanhd64(0.dd) != 0.dd) return 5;
    if (!approx_eqd64(sinhd64(1.dd), 1.17520119364380dd, 1e-9dd)) return 6;
    if (!approx_eqd64(coshd64(1.dd), 1.54308063481524dd, 1e-9dd)) return 7;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_erf_gamma(void) {
    if (erfd64(0.dd) != 0.dd) return 1;
    if (!approx_eqd64(erfcd64(0.dd), 1.dd, 1e-15dd)) return 2;
    if (!approx_eqd64(tgammad64(5.dd), 24.dd, 1e-9dd)) return 3; // 4!
    if (!approx_eqd64(lgammad64(1.dd), 0.dd, 1e-9dd)) return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_rounding(void) {
    if (ceild64(1.1dd) != 2.dd) return 1;
    if (ceild64(-1.1dd) != -1.dd) return 2;
    if (floord64(1.9dd) != 1.dd) return 3;
    if (floord64(-1.1dd) != -2.dd) return 4;
    if (truncd64(1.9dd) != 1.dd) return 5;
    if (truncd64(-1.9dd) != -1.dd) return 6;
    if (roundd64(1.5dd) != 2.dd) return 7;
    if (roundd64(-1.5dd) != -2.dd) return 8; // ties away from zero
    if (nearbyintd64(2.5dd) != 2.dd) return 9; // ties to even (default rounding)
    if (rintd64(1.dd) != 1.dd) return 10;
    if (ceild32(1.1df) != 2.df) return 11;
    if (ceild128(1.1dl) != 2.dl) return 12;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_int_valued(void) {
    if (ilogbd64(100.dd) != 2) return 1;
    if (lrintd64(3.dd) != 3) return 2;
    if (llrintd64(3.dd) != 3) return 3;
    if (lroundd64(1.5dd) != 2) return 4;
    if (llroundd64(1.5dd) != 2) return 5;
    if (quantexpd64(1.00dd) != -2) return 6;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_outparam(void) {
    int exp;
    _Decimal64 mant = frexpd64(8.dd, &exp);
    _Decimal64 recombined = scalbnd64(mant, exp);
    if (recombined != 8.dd) return 1;

    _Decimal64 ip;
    _Decimal64 frac = modfd64(3.5dd, &ip);
    if (ip != 3.dd) return 2;
    if (frac != 0.5dd) return 3;

    _Decimal128 ip128;
    _Decimal128 frac128 = modfd128(3.5dl, &ip128);
    if (ip128 != 3.dl) return 4;
    if (frac128 != 0.5dl) return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_scale(void) {
    if (scalbnd64(3.dd, 2) != 300.dd) return 1;
    if (scalblnd64(3.dd, 2) != 300.dd) return 2;
    if (ldexpd64(3.dd, 2) != 300.dd) return 3;
    // stepping toward a larger value must move strictly upward
    if (nextafterd64(1.dd, 2.dd) <= 1.dd) return 4;
    // stepping toward itself must be a no-op
    if (nextafterd64(1.dd, 1.dd) != 1.dd) return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_quantum(void) {
    _Decimal64 x = 1.dd;
    _Decimal64 q = quantized64(x, 1.00dd);
    if (q != 1.00dd) return 1;
    if (!samequantumd64(1.00dd, 2.00dd)) return 2;
    if (samequantumd64(1.0dd, 1.00dd)) return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_predicates(void) {
    _Decimal64 nanval = 0.dd / 0.dd;
    _Decimal64 infval = 1.dd / 0.dd;

    if (!isnand64(nanval)) return 1;
    if (isnand64(1.dd)) return 2;
    if (!isinfd64(infval)) return 3;
    if (isinfd64(1.dd)) return 4;
    if (!isfinited64(1.dd)) return 5;
    if (isfinited64(infval)) return 6;
    if (isfinited64(nanval)) return 7;
    if (!isnormald64(1.dd)) return 8;
    if (!signbitd64(-1.dd)) return 9;
    if (signbitd64(1.dd)) return 10;

    if (fpclassifyd64(0.dd) != FP_ZERO) return 11;
    if (fpclassifyd64(nanval) != FP_NAN) return 12;
    if (fpclassifyd64(infval) != FP_INFINITE) return 13;
    if (fpclassifyd64(1.dd) != FP_NORMAL) return 14;

    if (!isnand32((_Decimal32)nanval)) return 15;
    if (!isnand128((_Decimal128)nanval)) return 16;

    return 42;
}

[[cccc::test(return = 42)]]
int test_decimal_math_generic_dispatch(void) {
    // isnan/isinf/signbit/fpclassify/isnormal/isfinite from <math.h> must
    // dispatch correctly for float, double, AND _Decimal32/64/128 through
    // the same _Generic macros (#828 extends #778's existing dispatch).
    _Decimal64 nanval = 0.dd / 0.dd;

    if (isnan(nanval) != 1) return 1;
    if (isnan(1.0) != 0) return 2;
    if (isnan(1.0f) != 0) return 3;

    if (isinf(1.dd / 0.dd) != 1) return 4;
    if (isinf(1.0) != 0) return 5;

    if (signbit(-1.dd) == 0) return 6;
    if (signbit(-1.0) == 0) return 7;

    if (fpclassify(0.dd) != FP_ZERO) return 8;
    if (fpclassify(0.0) != FP_ZERO) return 9;

    if (!isfinite(1.dd)) return 10;
    if (isfinite(nanval)) return 11;
    if (!isfinite(1.0)) return 12;

    if (!isnormal(1.dd)) return 13;
    if (!isnormal(1.0)) return 14;
    // isnormal's false cases matter: an earlier draft of the decimal
    // _Generic dispatch evaluated to an always-truthy function pointer
    // instead of actually calling anything (caught only by asserting the
    // negative here, not just the positive above).
    if (isnormal(0.dd)) return 15;
    if (isnormal(nanval)) return 16;
    if (isnormal(0.0)) return 17;

    return 42;
}

// Op coverage for the math1/math2/mathi entries the tests above don't
// otherwise exercise -- op numbers are hand-mirrored between
// include/decimal_math.h and src/stdlib/decimal_math.c with no
// compile-time link between them, so an untested op can silently mean a
// mis-mapped one. NEXTTOWARD matters most here: it's the only op whose
// second operand is a fixed _Decimal128 regardless of the first operand's
// width.
[[cccc::test(return = 42)]]
int test_decimal_math_op_coverage(void) {
    if (acosd64(1.dd) != 0.dd) return 1;
    if (!approx_eqd64(acoshd64(1.dd), 0.dd, 1e-15dd)) return 2;
    if (logbd64(100.dd) != 2.dd) return 3;
    if (quantumd64(1.00dd) != 0.01dd) return 4;

    if (fmodd64(7.dd, 3.dd) != 1.dd) return 5;
    if (remainderd64(7.dd, 3.dd) != 1.dd) return 6;
    if (copysignd64(3.dd, -1.dd) != -3.dd) return 7;
    if (nexttowardd64(1.dd, 2.dl) <= 1.dd) return 8;
    if (nexttowardd64(1.dd, 1.dl) != 1.dd) return 9;

    if (issignalingd64(1.dd)) return 10; // a normal value must never signal

    if (!totalorderd64(1.dd, 2.dd)) return 11;
    if (totalorderd64(2.dd, 1.dd)) return 12;

    return 42;
}

#endif // __STDC_IEC_60559_DFP__
