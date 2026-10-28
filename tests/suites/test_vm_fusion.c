// CCCC_FLAGS: --testing
// Fused indexed load/store and integer multiply-add: values must match the
// plain load/add/multiply sequence for every element type and index shape.

typedef enum { E_A = -2, E_B = 5 } fz_enum;
struct fz_s {
    int   pad;
    int   arr[8];
    short sh[4];
};
struct fz_wide {
    int  a;
    char b;
    long c;
};

static int fz_calls;
static int fz_ident(int v) {
    fz_calls++;
    return v;
}
static int *fz_ptr(int *p) {
    fz_calls++;
    return p;
}

[[cccc::test]]
void test_load_each_element_type(void) {
    signed char    sc[4] = {-1, 2, -3, 4};
    unsigned char  uc[4] = {250, 2, 3, 4};
    bool           bo[4] = {false, true, true, false};
    short          sh[4] = {-300, 2, -3, 4};
    unsigned short us[4] = {60000, 2, 3, 4};
    int            in[4] = {-70000, 2, -3, 4};
    unsigned       un[4] = {4000000000u, 2, 3, 4};
    long           lo[4] = {-5000000000L, 2, -3, 4};
    float          fl[4] = {-1.5f, 2.25f, -3.0f, 4.0f};
    double         db[4] = {-1.5, 2.25, -3.0, 4.0};
    fz_enum        en[4] = {E_A, E_B, E_A, E_B};
    int           *pt[4] = {in, in + 1, in + 2, in + 3};
    _BitInt(12) bi[4]    = {-100, 2, -3, 4};
    for (int i = 0; i < 4; i++) {
        if (i == 0) {
            AssertEq(sc[i], -1);
            AssertEq(uc[i], 250);
            AssertEq((int)bo[i], 0);
            AssertEq(sh[i], -300);
            AssertEq(us[i], 60000);
            AssertEq(in[i], -70000);
            AssertTrue(un[i] == 4000000000u);
            AssertTrue(lo[i] == -5000000000L);
            AssertTrue(fl[i] == -1.5f);
            AssertTrue(db[i] == -1.5);
            AssertEq((int)en[i], -2);
            AssertTrue(pt[i] == in);
            AssertEq((int)bi[i], -100);
        } else if (i == 1) {
            AssertEq((int)bo[i], 1);
            AssertEq(sh[i], 2);
            AssertEq((int)en[i], 5);
            AssertTrue(pt[i] == in + 1);
            AssertTrue(fl[i] == 2.25f);
            AssertTrue(db[i] == 2.25);
        }
    }
}

[[cccc::test]]
void test_store_each_element_type_keeps_neighbours(void) {
    _Alignas(16) unsigned char guard[16];
    for (int g = 0; g < 16; g++)
        guard[g] = 0xAA;
    short *sp = (short *)(guard + 4);
    int   *ip = (int *)(guard + 8);
    char  *cp = (char *)(guard + 2);
    long  *lp = (long *)guard;
    for (int i = 0; i < 1; i++) {
        cp[i] = 0x11;
        sp[i] = 0x2222;
        ip[i] = 0x33333333;
    }
    AssertEq(guard[1], 0xAA);
    AssertEq(guard[2], 0x11);
    AssertEq(guard[3], 0xAA);
    AssertEq(guard[4], 0x22);
    AssertEq(guard[5], 0x22);
    AssertEq(guard[6], 0xAA);
    AssertEq(guard[8], 0x33);
    AssertEq(guard[11], 0x33);
    AssertEq(guard[12], 0xAA);
    for (int i = 0; i < 1; i++)
        lp[i] = 0;
    AssertEq(guard[7], 0);
    AssertEq(guard[8], 0x33);

    float  f[3] = {0, 0, 0};
    double d[3] = {0, 0, 0};
    for (int i = 1; i < 3; i++) {
        f[i] = 0.5f * i;
        d[i] = 0.25 * i;
    }
    AssertTrue(f[0] == 0 && f[1] == 0.5f && f[2] == 1.0f);
    AssertTrue(d[0] == 0 && d[1] == 0.25 && d[2] == 0.5);
}

[[cccc::test]]
void test_index_widths_and_casts(void) {
    int           a[8] = {10, 11, 12, 13, 14, 15, 16, 17};
    int          *mid  = a + 4;

    long          l    = 3;
    int           n    = -2;
    unsigned      u    = 5;
    unsigned char uc   = 7;
    short         s    = 1;
    AssertEq(a[l], 13);
    AssertEq(mid[n], 12);
    AssertEq(a[u], 15);
    AssertEq(a[uc], 17);
    AssertEq(a[s], 11);
    AssertEq(mid[-1], 13);

    // A narrowing cast truncates the index before it is scaled.
    long big = 0x100000002L;
    AssertEq(a[(int)big], 12);
    int wide = 0x103;
    AssertEq(a[(unsigned char)wide], 13);
    AssertEq(mid[(signed char)0xFE], 12);
}

[[cccc::test]]
void test_unsigned_index_wraps_in_its_own_width(void) {
    int      a[4] = {100, 101, 102, 103};
    int     *mid  = a + 1;
    char     c[4] = {'a', 'b', 'c', 'd'};
    char    *cmid = c + 1;
    unsigned top  = 0xFFFFFFFFu;
    // top + 1u wraps to 0 in 32 bits; the index register must not keep bit 32.
    AssertEq(mid[top + 1u], 101);
    AssertEq(cmid[top + 1u], 'b');
    unsigned n = 0xFFFFFFFFu;
    int      k = mid[n++ + 1u];
    AssertEq(k, 101);
}

[[cccc::test]]
void test_folded_constant_offsets(void) {
    int a[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int i    = 3;
    AssertEq(a[i + 1], 4);
    AssertEq(a[i - 1], 2);
    AssertEq(a[1 + i], 4);
    AssertEq(a[i + 4 - 2], 5);
    AssertEq(a[2], 2);
    AssertEq(a[7], 7);
    for (i = 1; i < 6; i++)
        a[i + 1] = a[i - 1] + a[i + 1];
    AssertEq(a[2], 2);
    AssertEq(a[3], 4);
    AssertEq(a[4], 6);
    AssertEq(a[5], 9);
    AssertEq(a[6], 12);

    unsigned long ul = 2;
    AssertEq(a[ul + 1], a[3]);
    unsigned u = 0xFFFFFFFFu;
    int     *p = a + 1;
    AssertEq(p[u + 1u], a[1]);
}

[[cccc::test]]
void test_member_arrays_and_struct_elements(void) {
    struct fz_s  s  = {1, {10, 11, 12, 13, 14, 15, 16, 17}, {20, 21, 22, 23}};
    struct fz_s *ps = &s;
    unsigned     u  = 5;
    int          i  = 2;
    AssertEq(s.arr[i], 12);
    AssertEq(ps->arr[u], 15);
    AssertEq(ps->arr[i + 1], 13);
    AssertEq(ps->sh[i], 22);
    ps->arr[u]    = 99;
    ps->sh[i + 1] = -7;
    AssertEq(s.arr[5], 99);
    AssertEq(s.sh[3], -7);
    AssertEq(s.pad, 1);

    struct fz_wide w[3] = {{1, 'a', 10}, {2, 'b', 20}, {3, 'c', 30}};
    for (i = 0; i < 3; i++) {
        w[i].a += 100;
        w[i].c  = w[i].c * 2;
    }
    AssertEq(w[0].a, 101);
    AssertEq(w[2].a, 103);
    AssertTrue(w[1].c == 40);
    AssertEq(w[1].b, 'b');
    struct fz_wide *pw = w + 1;
    AssertEq(pw[1].a, 103);
    AssertEq(pw[-1].b, 'a');
}

[[cccc::test]]
void test_two_dimensional_arrays(void) {
    int m[3][4];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = i * 10 + j;
    int i = 2, j = 3;
    AssertEq(m[i][j], 23);
    AssertEq(m[i][2], 22);
    AssertEq(m[1][j], 13);
    AssertEq(m[i - 1][j - 1], 12);
    int (*row)[4] = m + i;
    AssertEq(row[0][j], 23);
    AssertEq(row[-1][j], 13);
}

static int  fz_g[6]  = {5, 4, 3, 2, 1, 0};
static char fz_gc[4] = {'w', 'x', 'y', 'z'};

[[cccc::test]]
void test_global_and_static_arrays(void) {
    static short st[3] = {7, 8, 9};
    int          i     = 4;
    AssertEq(fz_g[i], 1);
    AssertEq(fz_g[i - 3], 4);
    AssertEq(fz_gc[i - 1], 'z');
    fz_g[i] = 50;
    AssertEq(fz_g[4], 50);
    AssertEq(st[i - 2], 9);
}

[[cccc::test]]
void test_calls_in_base_or_index(void) {
    int a[4] = {1, 2, 3, 4};
    fz_calls = 0;
    AssertEq(a[fz_ident(2)], 3);
    AssertEq(fz_calls, 1);
    a[fz_ident(1)] = 20;
    AssertEq(a[1], 20);
    AssertEq(fz_ptr(a)[fz_ident(3)], 4);
    fz_ptr(a)[0] = fz_ident(9);
    AssertEq(a[0], 9);
    AssertEq(fz_calls, 6);
    // The value is computed before the address, which holds a call.
    a[fz_ident(2)] = fz_ident(a[0]) + 1;
    AssertEq(a[2], 10);
}

[[cccc::test]]
void test_discarded_and_conditional_loads(void) {
    int a[4] = {1, 2, 3, 4};
    int i    = 2;
    (void)a[i];
    if (a[i] > 2)
        a[i + 1] = 0;
    AssertEq(a[3], 0);
    int sum = 0;
    for (i = 0; i < 4; i++)
        sum += a[i];
    AssertEq(sum, 6);
    i   = 0;
    sum = 0;
    while (i < 4)
        sum += a[i++];
    AssertEq(sum, 6);
}

[[cccc::test]]
void test_unsigned_multiply_add_wraps(void) {
    unsigned x = 5u, y = 0xFFFFFFFFu, z = 3u;
    unsigned r = x + y * z;
    AssertTrue(r == 2u);
    r = y * z + x;
    AssertTrue(r == 2u);
    unsigned long lx = 1, ly = 0xFFFFFFFFFFFFFFFFul, lz = 2;
    AssertTrue(lx + ly * lz == 0xFFFFFFFFFFFFFFFFul);
    unsigned short us = 65535;
    int            ui = us + us * 2;
    AssertEq(ui, 196605);
}

[[cccc::test]]
void test_signed_multiply_add_forms(void) {
    int  x = 100, y = 7, z = -3;
    long lx = 1000000, ly = 3000000;
    AssertEq(x + y * z, 79);
    AssertEq(y * z + x, 79);
    AssertEq(x + y * 5, 135);
    AssertEq(x + 5 * y, 135);
    AssertEq(x + y * -4, 72);
    AssertEq(y * 0 + x, 100);
    AssertEq(x + y * 1, 107);
    AssertEq(y * 6, 42);
    AssertEq(-3 * y, -21);
    AssertEq(x * y + z * y, 679);
    AssertTrue(lx + ly * 4000L == 12001000000L);
    AssertTrue(lx + ly * 5000000000L == 15000000001000000L);
    char cx = 100, cy = 2, cz = 100;
    AssertEq((char)(cx + cy * cz), (char)300);
    short sx = 30000, sy = 2;
    AssertEq((short)(sx + sy * sx), (short)90000);
}

[[cccc::test]]
void test_pointer_sum_multiply_add(void) {
    int  a[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    long j     = 2;
    int *p     = a + j * 3;
    AssertEq(*p, 6);
    int (*r)[4] = (int (*)[4])a + j;
    AssertEq(r[0][1], 9);
    char  buf[10] = "abcdefghi";
    char *q       = buf + j * 2 + 1;
    AssertEq(*q, 'f');
    struct fz_wide  w[3] = {{1, 'a', 1}, {2, 'b', 2}, {3, 'c', 3}};
    struct fz_wide *pw   = w + j;
    AssertEq(pw->b, 'c');
}

[[cccc::test]]
void test_many_live_temporaries(void) {
    int a[16];
    for (int i = 0; i < 16; i++)
        a[i] = i;
    int i = 1, j = 2, k = 3;
    int v = a[i] + a[j] * a[k] + a[i + 1] * a[j + 1] + a[k + 2] * a[i + 3] +
            a[a[i] + a[j]] * a[a[j] + a[k]];
    AssertEq(v, 1 + 6 + 6 + 20 + 15);
}
