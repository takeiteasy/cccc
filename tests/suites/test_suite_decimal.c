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

#endif // __STDC_IEC_60559_DFP__
