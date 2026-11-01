// CCCC_FLAGS: --testing

// Bit-field reads, writes and source-level shift-and-mask extracts, each
// against a known value or the same operation with the count in a variable.

typedef struct {
    unsigned a : 3;
    int      b : 5;
    unsigned c : 1;
    int      d : 7;
    unsigned e : 16;
    int      f : 31;
    unsigned g : 1;
} Flags;

typedef struct {
    long long          lo : 40;
    unsigned long long hi : 24;
} Wide;

typedef struct {
    unsigned long long all : 64;
} Full;

typedef struct {
    long long x   : 1;
    long long mid : 62;
    long long y   : 1;
} Edge;

[[cccc::test]]
void test_fields_round_trip(void) {
    Flags             s  = {0};
    volatile unsigned va = 5, vc = 1, ve = 0xabcd, vg = 1;
    volatile int      vb = -9, vd = 63, vf = -1000000;
    s.a = va;
    s.b = vb;
    s.c = vc;
    s.d = vd;
    s.e = ve;
    s.f = vf;
    s.g = vg;
    AssertEq((int)s.a, 5);
    AssertEq(s.b, -9);
    AssertEq((int)s.c, 1);
    AssertEq(s.d, 63);
    AssertEq((int)s.e, 0xabcd);
    AssertEq(s.f, -1000000);
    AssertEq((int)s.g, 1);
}

[[cccc::test]]
void test_store_keeps_neighbours(void) {
    Flags             s     = {0};
    volatile unsigned all_a = 7, all_e = 0xffff;
    volatile int      all_b = -1, all_d = -1, zero = 0;
    s.a = all_a;
    s.b = all_b;
    s.c = 1;
    s.d = all_d;
    s.e = all_e;
    s.b = zero;
    AssertEq((int)s.a, 7);
    AssertEq(s.b, 0);
    AssertEq((int)s.c, 1);
    AssertEq(s.d, -1);
    AssertEq((int)s.e, 0xffff);
    s.d = zero;
    s.a = zero;
    AssertEq((int)s.a, 0);
    AssertEq((int)s.c, 1);
    AssertEq(s.d, 0);
    AssertEq((int)s.e, 0xffff);
}

[[cccc::test]]
void test_store_truncates_to_the_width(void) {
    Flags        s   = {0};
    volatile int v16 = 16, v31 = 31, v9 = 9, v64 = 64;
    s.b = v16;
    AssertEq(s.b, -16);
    s.b = v31;
    AssertEq(s.b, -1);
    s.a = v9;
    AssertEq((int)s.a, 1);
    s.d = v64;
    AssertEq(s.d, -64);
    AssertEq((int)s.c, 0);
}

[[cccc::test]]
void test_wide_fields(void) {
    Wide                        w = {0};
    volatile long long          a = 0x7fffffffffLL, b = 0x8000000000LL, c = -2;
    volatile unsigned long long h = 0xffffffULL;
    w.hi                          = h;
    w.lo                          = c;
    AssertEq(w.lo, -2LL);
    AssertEq((long long)w.hi, 0xffffffLL);
    w.lo = a;
    AssertEq(w.lo, 0x7fffffffffLL);
    w.lo = b;
    AssertEq(w.lo, -0x8000000000LL);
    AssertEq((long long)w.hi, 0xffffffLL);
}

[[cccc::test]]
void test_full_width_field(void) {
    Full                        f = {0};
    volatile unsigned long long v = 0xfedcba9876543210ULL;
    f.all                         = v;
    AssertEq(f.all == 0xfedcba9876543210ULL, true);
}

[[cccc::test]]
void test_single_bit_and_edge_fields(void) {
    Edge               e  = {0};
    volatile long long m1 = -1, zero = 0, mid = 0x1234567890abcdeLL;
    e.y   = m1;
    e.mid = mid;
    AssertEq(e.x, 0LL);
    AssertEq(e.y, -1LL);
    AssertEq(e.mid, 0x1234567890abcdeLL);
    e.x = m1;
    e.y = zero;
    AssertEq(e.x, -1LL);
    AssertEq(e.y, 0LL);
    AssertEq(e.mid, 0x1234567890abcdeLL);
}

volatile unsigned long vul = 0xfedcba9876543210ul;
volatile unsigned      vui = 0xdeadbeefu;
volatile long          vsl = 0xff000;

[[cccc::test]]
void test_shift_and_mask(void) {
    int           s8 = 8, s52 = 52, s60 = 60;
    unsigned long m8 = 0xff, m12 = 0xfff, m8w = 0xff;
    AssertEq((vul >> 8) & 0xff, 0x32ul);
    AssertEq((vul >> 8) & 0xff, (vul >> s8) & m8);
    AssertEq((vul >> 52) & 0xfff, 0xfedul);
    AssertEq((vul >> 52) & 0xfff, (vul >> s52) & m12);
    AssertEq((vul >> 60) & 0xf, 0xful);
    AssertEq((vul >> 60) & 0xff, (vul >> s60) & m8w);
    AssertEq((vui >> 4) & 0xfff, 0xbeeu);
}

[[cccc::test]]
void test_shift_pair_sign_extends(void) {
    int n44 = 44, n52 = 52;
    AssertEq((vsl << 44) >> 52, -16L);
    AssertEq((vsl << 44) >> 52, (vsl << n44) >> n52);
    AssertEq((unsigned long)(vsl << 44) >> 52, 0xff0ul);
}
