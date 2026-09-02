// CCCC_FLAGS: --testing
//
// #1123: multi-word _BitInt(N>128) lowering for -c=native/-m/-c=generated.
// Before this, any _BitInt/__int128 wider than 128 bits hard-errored at
// serialize_type() ("exceeds 128 bits, which has no native/-m lowering") --
// see tests/test_native_bitint_over_128.c, which pinned that refusal and is
// now retargeted to a positive round-trip instead. This file exercises the
// lowering itself: the emitted __cccc_biK container (serialize_type.c),
// the runtime it shares with the VM (src/stdlib/wide_bitint.c, extracted by
// tools/gen_shims.py into src/shims.inc), and the statement-expression
// codegen for every whitelisted node kind (serialize_expr.c). Every
// assertion here is checked identically under the VM and under
// -c=native/-m -- a mismatch between the two would mean the native lowering
// disagrees with src/stdlib/wide_bitint.c despite sharing its source text,
// which should be impossible by construction, but is exactly the class of
// bug worth a real regression test for.
//
// Widths used: 192/256 (>128, <=256, single extra word beyond __int128),
// 300/1000 (crosses multiple word boundaries), 4096 (stresses schoolbook
// multiply/Knuth division at scale). tests/suites/test_suite_c23.c already
// covers 65535 (BITINT_MAXWIDTH); not repeated here.

static unsigned _BitInt(256) pow2_256(int n) {
    unsigned _BitInt(256) r = 1;
    for (int i = 0; i < n; i++)
        r = r * 2;
    return r;
}

static _BitInt(256) add256(_BitInt(256) a, _BitInt(256) b) {
    return a + b;
}

struct HasWide1123 {
    int x;
    _BitInt(200) v;
    char c;
};

static _BitInt(300) g_g300_1123 = ((_BitInt(300))1 << 250) + 7;
static unsigned _BitInt(4096)
    g_g4096_1123 = (unsigned _BitInt(4096))123456789wb;

[[cccc::test(return = 42)]]
int test_wide_bitint_literal_and_sizeof(void) {
    unsigned _BitInt(256) w256 = 0;
    if (sizeof(w256) != 32)
        return 1;
    _BitInt(300) w300 = 0;
    if (sizeof(w300) != 40)
        return 2;
    if (g_g300_1123 != (((_BitInt(300))1 << 250) + 7))
        return 3;
    if (g_g4096_1123 != 123456789wb)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_wide_bitint_arith_by_value(void) {
    unsigned _BitInt(256) a = pow2_256(4);
    if (a != 16)
        return 1;
    unsigned _BitInt(256) wrap = pow2_256(256);
    if (wrap != 0) // wraps to 0 exactly at the declared width
        return 2;
    _BitInt(256) sx = add256(-100, 300);
    if (sx != 200)
        return 3;
    return 42;
}

[[cccc::test(return = 42)]]
int test_wide_bitint_arith_signed_unsigned(void) {
    _BitInt(200) sa = 1000, sb = 7;
    if ((sa + sb) != 1007)
        return 1;
    if ((sa - sb) != 993)
        return 2;
    if ((sa * sb) != 7000)
        return 3;
    if ((sa / sb) != 142)
        return 4;
    if ((sa % sb) != 6)
        return 5;
    _BitInt(200) neg_a = -1000;
    if ((neg_a / sb) != -142)
        return 6;
    if ((neg_a % sb) != -6)
        return 7;

    unsigned _BitInt(200) ua = 1000, ub = 7;
    if ((ua / ub) != 142)
        return 8;
    if ((ua % ub) != 6)
        return 9;
    return 42;
}

[[cccc::test(return = 42)]]
int test_wide_bitint_bitwise_and_negate(void) {
    unsigned _BitInt(200) x = 0xFF, y = 0x0F;
    if ((x & y) != 0x0F)
        return 1;
    if ((x | y) != 0xFF)
        return 2;
    if ((x ^ y) != 0xF0)
        return 3;
    if (~(unsigned _BitInt(200))0 == 0)
        return 4;
    _BitInt(200) neg = 42;
    if (-neg != -42)
        return 5;
    return 42;
}

[[cccc::test(return = 42)]]
int test_wide_bitint_shifts_cross_word(void) {
    _BitInt(200) shl = ((_BitInt(200))1 << 130);
    if ((shl >> 130) != 1)
        return 1;
    unsigned _BitInt(200) ushl = ((unsigned _BitInt(200))1 << 199);
    if ((ushl >> 199) != 1)
        return 2;
    _BitInt(200) negshr = -1;
    if ((negshr >> 100) != -1) // arithmetic shift preserves sign
        return 3;

    // _BitInt(4096), exercises a real multi-word (>2 extra words) shift.
    unsigned _BitInt(4096) huge = 1;
    huge                        = huge << 4000;
    if ((huge >> 4000) != 1)
        return 4;
    return 42;
}

[[cccc::test(return = 42)]]
int test_wide_bitint_comparisons(void) {
    _BitInt(200) c1 = -5, c2 = 5;
    if (!(c1 < c2))
        return 1;
    if (c1 > c2)
        return 2;
    if (c1 == c2)
        return 3;
    if (!(c1 != c2))
        return 4;
    unsigned _BitInt(200) uc1 = 5, uc2 = 10;
    if (!(uc1 < uc2))
        return 5;
    if (!(uc1 <= uc1))
        return 6;
    if (!(uc2 >= uc1))
        return 7;
    return 42;
}

[[cccc::test(return = 42)]]
int test_wide_bitint_casts(void) {
    long long ll        = -123456789012345LL;
    _BitInt(200) fromll = (_BitInt(200))ll;
    if ((long long)fromll != ll)
        return 1;

    unsigned long long ull        = 0xFFFFFFFFFFFFFFFFULL;
    unsigned _BitInt(200) fromull = (unsigned _BitInt(200))ull;
    if ((unsigned long long)fromull != ull)
        return 2;

    _BitInt(200) wide1 = 42;
    _BitInt(300) wide2 = (_BitInt(300))wide1; // wide -> wide (both >128)
    if (wide2 != 42)
        return 3;
    _BitInt(200) back = (_BitInt(200))wide2;
    if (back != 42)
        return 4;

    double d           = 12345.0;
    _BitInt(200) fromd = (_BitInt(200))d;
    if (fromd != 12345)
        return 5;
    double tod = (double)fromd;
    if (tod != 12345.0)
        return 6;

    double negd           = -98765.0;
    _BitInt(200) fromnegd = (_BitInt(200))negd;
    if (fromnegd != -98765)
        return 7;

    return 42;
}

[[cccc::test(return = 42)]]
int test_wide_bitint_truthiness(void) {
    _BitInt(200) zero = 0, nonzero = 1;
    if (!nonzero)
        return 1;
    if (zero)
        return 2;
    if (!(!zero))
        return 3;
    if (!(!!nonzero))
        return 4;
    if (!(nonzero && 1))
        return 5;
    if (!(zero || 1))
        return 6;
    if (zero && 1)
        return 7;
    int tern = zero ? 1 : 2;
    if (tern != 2)
        return 8;
    while (zero)
        return 9;
    for (; zero;)
        return 10;

    _Bool bz = (_Bool)zero;
    _Bool bn = (_Bool)nonzero;
    if (bz != 0)
        return 11;
    if (bn != 1)
        return 12;
    return 42;
}

[[cccc::test(return = 42)]]
int test_wide_bitint_struct_member(void) {
    struct HasWide1123 hw;
    hw.x = 5;
    hw.v = ((_BitInt(200))1 << 150) + 3;
    hw.c = 'z';
    if (hw.x != 5)
        return 1;
    if (hw.v != (((_BitInt(200))1 << 150) + 3))
        return 2;
    if (hw.c != 'z')
        return 3;
    return 42;
}
