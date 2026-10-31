// CCCC_FLAGS: --testing

// Shifts and AND by a constant take an immediate. Each case checks the
// constant form against the same operation with the count or mask in a
// variable, and against a known value.

volatile int           vsi = -12345;
volatile unsigned      vui = 0xdeadbeefu;
volatile long          vsl = -0x123456789abcdefL;
volatile unsigned long vul = 0xfedcba9876543210ul;

[[cccc::test]]
void test_int_shift_counts(void) {
    int n0 = 0, n1 = 1, n31 = 31;
    AssertEq(vsi << 0, vsi << n0);
    AssertEq(vsi << 1, vsi << n1);
    AssertEq(vsi << 3, -98760);
    AssertEq(vsi >> 0, vsi >> n0);
    AssertEq(vsi >> 1, vsi >> n1);
    AssertEq(vsi >> 3, -1544);
    AssertEq(vsi >> 31, vsi >> n31);
    AssertEq(vsi >> 31, -1);
    AssertEq(1 << 30, 1073741824);
}

[[cccc::test]]
void test_unsigned_shift_counts(void) {
    int n1 = 1, n31 = 31;
    AssertEq(vui << 1, vui << n1);
    AssertEq(vui << 31, 2147483648u);
    AssertEq(vui << 31, vui << n31);
    AssertEq(vui >> 3, 466991069u);
    AssertEq(vui >> 31, 1u);
    AssertEq(vui >> 31, vui >> n31);
}

[[cccc::test]]
void test_long_shift_counts(void) {
    int n32 = 32, n63 = 63;
    AssertEq(vsl << 32, vsl << n32);
    AssertEq(vsl << 63, vsl << n63);
    AssertEq(vsl << 63, (-0x7fffffffffffffffL - 1));
    AssertEq(vsl >> 32, vsl >> n32);
    AssertEq(vsl >> 63, -1L);
    AssertEq(vsl >> 3, -10248191152060862L);
}

[[cccc::test]]
void test_unsigned_long_shift_counts(void) {
    int n32 = 32, n63 = 63;
    AssertEq(vul << 32, vul << n32);
    AssertEq(vul >> 32, vul >> n32);
    AssertEq(vul >> 63, 1ul);
    AssertEq(vul >> 63, vul >> n63);
    AssertEq(vul << 63, 0ul);
    AssertEq(vul >> 3, 2295594818061633090ul);
}

[[cccc::test]]
void test_shift_result_stays_int(void) {
    int x = 0x40000001;
    AssertEq(x << 1, (int)0x80000002u);
    AssertEq((x << 1) >> 1, (int)0xc0000001u);
    unsigned u = 0x80000001u;
    AssertEq(u << 1, 2u);
    AssertEq((u << 4) >> 4, 0x00000001u);
}

[[cccc::test]]
void test_and_masks(void) {
    int m = 0xff, neg4 = -4;
    AssertEq(vsi & 0xff, vsi & m);
    AssertEq(vsi & 0xff, 199);
    AssertEq(vsi & -4, vsi & neg4);
    AssertEq(vsi & -4, -12348);
    AssertEq(0xff & vsi, 199);
    AssertEq(vui & 0x7fffffff, 1588444911u);
    AssertEq(vul & 0xffffffffu, 1985229328ul);
    AssertEq(vsl & -1, vsl);
    AssertEq(vsl & 0, 0L);
}

[[cccc::test]]
void test_narrowing_after_immediate_op(void) {
    AssertEq((signed char)(vsi & 0xff), -57);
    AssertEq((int)(vul >> 32), (int)0xfedcba98u);
    AssertEq((short)(vui >> 8), (short)0xadbe);
    int n1 = 1;
    AssertEq((int)(vsl >> 1), (int)(vsl >> n1));
}

[[cccc::test]]
void test_shifted_range_feeds_narrowing(void) {
    short a[2] = {-12345, 7};
    int   sum = 0, sgn = 0;
    for (int i = 0; i < 2; i++) {
        short s  = a[i];
        sum     += (unsigned char)(s >> 8);
        sgn     += (signed char)(s >> 8);
    }
    AssertEq(sum, 207);
    AssertEq(sgn, -49);
}

// The constants sit in promoted locals, so the bytecode clean-up folds the
// operation to its value.
long fold_shl(long n) {
    long x = 200, r = 0;
    for (int k = 0; k < n; k++)
        r = x << 4;
    return r;
}

long fold_shr(long n) {
    long x = 200, r = 0;
    for (int k = 0; k < n; k++)
        r = x >> 3;
    return r;
}

long fold_sar(long n) {
    long x = -200, r = 0;
    for (int k = 0; k < n; k++)
        r = x >> 3;
    return r;
}

unsigned long fold_ushr(long n) {
    unsigned long x = 0xfffffffffffffff8ul, r = 0;
    for (int k = 0; k < n; k++)
        r = x >> 60;
    return r;
}

long fold_and(long n) {
    long x = 200, r = 0;
    for (int k = 0; k < n; k++)
        r = x & 0x3c;
    return r;
}

[[cccc::test]]
void test_folded_immediate_ops(void) {
    AssertEq(fold_shl(2), 3200L);
    AssertEq(fold_shr(2), 25L);
    AssertEq(fold_sar(2), -25L);
    AssertEq(fold_ushr(2), 15ul);
    AssertEq(fold_and(2), 8L);
}

[[cccc::test]]
void test_mask_keeps_int_range(void) {
    long v = 0x100000005L;
    AssertEq((int)(v & 0x7FFFFFFF), 5);
    unsigned long w = 0xffffffff00000007ul;
    AssertEq(w & 0xffu, 7ul);
}

[[cccc::test]]
void test_compound_assign_promoted(void) {
    unsigned a = 0x1234u;
    for (int i = 0; i < 3; i++) {
        a <<= 4;
        a  &= 0xfffffu;
        a >>= 1;
    }
    AssertEq(a, 0x46800u);
}

[[cccc::test]]
void test_compound_assign_framed(void) {
    int  c   = 33;
    int *p   = &c;
    c      <<= 3;
    c       &= 0xff;
    c      >>= 1;
    AssertEq(*p, 4);
    long  s   = -1000;
    long *q   = &s;
    s       >>= 4;
    s        &= ~0xfL;
    AssertEq(*q, -64);
}

[[cccc::test]]
void test_wide_multiplier(void) {
    unsigned long m = 3;
    unsigned long k = 6364136223846793005ul;
    AssertEq(m * 6364136223846793005ul, m * k);
    AssertEq(m * 6364136223846793005ul + 1, m * k + 1);
    AssertEq(7ul + m * 6364136223846793005ul, 7ul + m * k);
    int i = 7;
    AssertEq(i * 65536 * 32768, (int)0x80000000u);
    AssertEq(i * -5, -35);
    AssertEq(i * 4000000000u, i * 4000000000u);
}

struct Bits {
    unsigned      a : 3;
    int           b : 5;
    unsigned long c : 40;
    int           d : 7;
};

[[cccc::test]]
void test_bitfields(void) {
    struct Bits s = {0};
    s.a           = 5;
    s.b           = -9;
    s.c           = 0xabcdef1234ul;
    s.d           = -60;
    AssertEq(s.a, 5u);
    AssertEq(s.b, -9);
    AssertEq(s.c, 0xabcdef1234ul);
    AssertEq(s.d, -60);
    s.b += 3;
    s.a ^= 7;
    s.d *= 2;
    AssertEq(s.a, 2u);
    AssertEq(s.b, -6);
    AssertEq(s.c, 0xabcdef1234ul);
    AssertEq(s.d, 8);
}

[[cccc::test]]
void test_bitint_narrow(void) {
    _BitInt(13) a = 4000;
    a             = a + 5000;
    AssertEq((int)a, 808);
    unsigned _BitInt(9) b  = 500;
    b                     += 100;
    AssertEq((unsigned)b, 88u);
}
