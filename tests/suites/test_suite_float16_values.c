// CCCC_FLAGS: --testing
// _Float16 values: literals, conversions, arithmetic, comparisons, truth
// tests, calls and initializers. Expected bit patterns come from a host
// compiler with a native _Float16.

#include <float.h>
#include <math.h>
#include <string.h>

typedef _Float16 half;

static unsigned bits(_Float16 h) {
    unsigned short b;
    memcpy(&b, &h, sizeof b);
    return b;
}

static _Float16 half_of(unsigned b) {
    unsigned short s = (unsigned short)b;
    _Float16       h;
    memcpy(&h, &s, sizeof h);
    return h;
}

static _Float16 add(_Float16 a, _Float16 b) {
    return a + b;
}
static _Float16 pick(int i, _Float16 a, double d, _Float16 b, float f) {
    return i ? a * (_Float16)d + b + (_Float16)f : a;
}
static half twice(half h) {
    return h * 2;
}

struct Pair {
    _Float16 lo;
    _Float16 hi;
};
static struct Pair make_pair(_Float16 a, _Float16 b) {
    struct Pair p = {a, b};
    return p;
}

static _Float16       g_one   = 1;
static _Float16       g_neg   = -2.5f16;
static _Float16       g_arr[] = {1, 0.5f16, -3, 65504.0f16};
static const _Float16 g_inf   = 1.0f16 / 0;
static struct Pair    g_pair  = {0.25f16, 7};

[[cccc::test(return = 42)]]
int test_literals(void) {
    if (bits(0.1f16) != 0x2e66 || bits(0.2f16) != 0x3266)
        return 1;
    if (bits(65504.0f16) != 0x7bff || bits(1e10f16) != 0x7c00)
        return 2;
    if (bits(0x1.8p1F16) != 0x4200)
        return 3;
    if ((double)0.1f16 != 0.0999755859375)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_conversions_round_once(void) {
    volatile int    i2049 = 2049, i2047 = 2047;
    volatile double d2 = 65520.0, tie = 1.0 + 1.0 / 2048, d1 = 0.1;
    if (bits((_Float16)i2049) != 0x6800 || bits((_Float16)i2047) != 0x67ff)
        return 1;
    if (bits((_Float16)d2) != 0x7c00)
        return 2;
    if (bits((_Float16)tie) != 0x3c00)
        return 3;
    if (bits((_Float16)d1) != 0x2e66)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_conversions_from_half(void) {
    _Float16 h = 3.75f16;
    if ((int)h != 3 || (long long)h != 3 || (unsigned)h != 3)
        return 1;
    if ((float)h != 3.75f || (double)h != 3.75 || (long double)h != 3.75L)
        return 2;
    if ((int)-h != -3)
        return 3;
    if ((_Bool)h != 1 || (_Bool)(_Float16)0 != 0)
        return 4;
    if ((unsigned long long)65504.0f16 != 65504ULL)
        return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_arithmetic_rounds_each_operation(void) {
    volatile _Float16 a = 0.1f16, b = 0.2f16;
    if (bits(a + b) != 0x34cc || bits(a - b) != 0xae66)
        return 1;
    if (bits(a * b) != 0x251e || bits(a / b) != 0x3800)
        return 2;
    if (bits(a * b + a) != 0x2fae)
        return 3;
    volatile _Float16 one = 1, three = 3;
    if (bits(one / three) != 0x3555)
        return 4;
    if (bits(-a) != 0xae66)
        return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_mixed_operands(void) {
    volatile _Float16 big = 65504.0f16, a = 0.1f16;
    volatile int      i70 = 70000;
    if (bits(big + i70) != 0x7c00)
        return 1;
    if ((double)(a + 0.5f) != 0x1.333p-1)
        return 2;
    if ((double)(a + 0.5) != 0x1.333p-1)
        return 3;
    if (_Generic(a + 0.5f, float: 1, default: 0) != 1)
        return 4;
    if (_Generic(a + 1, _Float16: 1, default: 0) != 1)
        return 5;
    if (bits(a + 1) != 0x3c66)
        return 6;
    return 42;
}

[[cccc::test(return = 42)]]
int test_compound_and_increment(void) {
    _Float16 h  = 1;
    h          += 0.1f16;
    if (bits(h) != 0x3c66)
        return 1;
    h = 2047;
    h++;
    if (bits(h) != 0x6800)
        return 2;
    h = 2048;
    h++;
    if (bits(h) != 0x6800)
        return 3;
    h  = 5;
    h *= 3;
    h -= 1;
    h /= 2;
    if ((int)h != 7)
        return 4;
    _Float16 r = h--;
    if ((int)r != 7 || (int)h != 6)
        return 5;
    r = ++h;
    if ((int)r != 7 || (int)h != 7)
        return 6;
    return 42;
}

[[cccc::test(return = 42)]]
int test_comparisons(void) {
    volatile _Float16 a = 0.1f16, b = 0.2f16, z = -0.0f16, pz = 0.0f16;
    volatile _Float16 nan = (_Float16)NAN, inf = g_inf;
    if (!(a < b) || !(a <= b) || a > b || a >= b || a == b || !(a != b))
        return 1;
    if (!(z == pz) || z != pz || z < pz || !(z <= pz))
        return 2;
    if (nan == nan || !(nan != nan) || nan < a || nan > a || nan <= a)
        return 3;
    if (!(inf > b) || !(-inf < b))
        return 4;
    if (!(a < 1) || !(a == 0.0999755859375) || !(a < 0.1f))
        return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_truth_tests(void) {
    volatile _Float16 z = -0.0f16, one = 1, tiny = 5.9604644775390625e-8f16;
    volatile _Float16 nan = (_Float16)NAN;
    int               n   = 0;
    if (z)
        return 1;
    if (!one)
        return 2;
    if (!tiny || !nan)
        return 3;
    if (!(one && tiny) || z || (z && one) || !(z || one))
        return 4;
    n += z ? 100 : 1;
    n += one ? 1 : 100;
    while (z) {
        n += 100;
        break;
    }
    for (volatile _Float16 f = 2; f; f = f - 1)
        n++;
    do {
        n++;
    } while (z);
    return n == 5 && !z && !!one ? 42 : 5;
}

[[cccc::test(return = 42)]]
int test_calls_and_returns(void) {
    if ((int)add(1.5f16, 2.5f16) != 4)
        return 1;
    if ((int)add(add(1, 2), add(3, 4)) != 10)
        return 2;
    if (bits(twice(half_of(0x3c00))) != 0x4000)
        return 3;
    if ((double)pick(1, 2, 0.5, 3, 4.0f) != 8.0)
        return 4;
    if ((double)pick(0, 2, 0.5, 3, 4.0f) != 2.0)
        return 5;
    struct Pair p = make_pair(1.5f16, -2);
    if ((double)p.lo != 1.5 || (double)p.hi != -2)
        return 6;
    _Float16 (*fp)(_Float16, _Float16) = add;
    if ((int)fp(20, 22) != 42)
        return 7;
    return 42;
}

[[cccc::test(return = 42)]]
int test_initializers(void) {
    if ((double)g_one != 1 || (double)g_neg != -2.5)
        return 1;
    if ((double)g_arr[1] != 0.5 || (double)g_arr[2] != -3 ||
        (double)g_arr[3] != 65504)
        return 2;
    if (!isinf((double)g_inf))
        return 3;
    if ((double)g_pair.lo != 0.25 || (double)g_pair.hi != 7)
        return 4;
    _Float16    loc[3] = {1, 2.5f16, -1};
    struct Pair lp     = {0.125f16, 9};
    _Float16    z[2]   = {0};
    if ((double)loc[1] != 2.5 || (double)loc[2] != -1)
        return 5;
    if ((double)lp.lo != 0.125 || (double)lp.hi != 9 || bits(z[1]) != 0)
        return 6;
    return 42;
}

[[cccc::test(return = 42)]]
int test_memory(void) {
    _Float16  arr[4]  = {1, 2, 3, 4};
    _Float16 *p       = arr;
    p[2]             += 10;
    *p++              = 5;
    if ((int)arr[0] != 5 || (int)arr[2] != 13 || (int)*p != 2)
        return 1;
    struct Pair  s  = {1, 2};
    struct Pair *ps = &s;
    ps->hi          = ps->lo + ps->hi * 4;
    return (int)s.hi == 9 ? 42 : 2;
}

[[cccc::test(return = 42)]]
int test_float_limits(void) {
    if ((double)FLT16_MAX != 65504 || (double)FLT16_MIN != 0.00006103515625)
        return 1;
    if ((double)FLT16_EPSILON != 0.0009765625 || FLT16_MANT_DIG != 11)
        return 2;
    if ((double)FLT16_TRUE_MIN != 5.9604644775390625e-8)
        return 3;
    if (!isinf((double)(FLT16_MAX * 2)))
        return 4;
    return 42;
}
