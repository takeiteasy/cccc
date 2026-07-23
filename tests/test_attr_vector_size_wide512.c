// 512-bit GNU vector_size support (tracker #722, follow-up to #715). Same
// coverage as test_attr_vector_size_wide256.c, scaled to the full 64-byte
// VReg width -- the widest substrate this tracker adds. Confirms the
// operand-carried lane count/byte width has no hidden 256-bit ceiling.

typedef float v16sf __attribute__((vector_size(64)));
typedef double v8df __attribute__((vector_size(64)));
typedef int v16si __attribute__((vector_size(64)));
typedef long v8di __attribute__((vector_size(64)));
typedef short v32hi __attribute__((vector_size(64)));
typedef char v64qi __attribute__((vector_size(64)));

static v16sf make16f(float base) {
    v16sf v;
    for (int i = 0; i < 16; i++) v[i] = base + (float)i;
    return v;
}

static float sum16f(v16sf v) {
    float s = 0.0f;
    for (int i = 0; i < 16; i++) s += v[i];
    return s;
}

int main(void) {
    // ---- float32 x16: arithmetic, negate, splat, compare, select ----
    v16sf a, b;
    for (int i = 0; i < 16; i++) { a[i] = (float)(i + 1); b[i] = (float)(10 * (i + 1)); }

    v16sf c = a + b;
    for (int i = 0; i < 16; i++)
        if (c[i] != (float)(i + 1) + (float)(10 * (i + 1))) return 1;

    v16sf d = b - a;
    if (d[0] != 9.0f) return 2;
    if (d[15] != 144.0f) return 3;

    v16sf e = a * 2.0f; // scalar broadcast (splat)
    if (e[0] != 2.0f) return 4;
    if (e[15] != 32.0f) return 5;

    v16sf f = b / a;
    for (int i = 0; i < 16; i++)
        if (f[i] != 10.0f) return 6;

    v16sf g = -a;
    if (g[0] != -1.0f) return 7;
    if (g[15] != -16.0f) return 8;

    v16si cmp = (a < (v16sf){5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5});
    v16sf sel = cmp ? a : b;
    if (sel[0] != 1.0f) return 9;    // a[0]=1 < 5 -> then-arm
    if (sel[15] != 160.0f) return 10; // a[15]=16 not< 5 -> else-arm

    // ---- double x8 ----
    v8df p, q;
    for (int i = 0; i < 8; i++) { p[i] = 1.5 + (double)i; q[i] = 0.5; }
    v8df r = p + q;
    if (r[0] != 2.0) return 11;
    if (r[7] != 9.0) return 12;

    // ---- int32 x16: arithmetic, bitwise, div/mod, convertvector ----
    v16si vi;
    for (int i = 0; i < 16; i++) vi[i] = (i % 2 == 0) ? (i + 1) : -(i + 1);
    v16si vi2 = vi * 3;
    if (vi2[0] != 3) return 13;
    if (vi2[1] != -6) return 14;
    v16si vi3 = -vi;
    if (vi3[1] != 2) return 15;

    v16si band_a, band_b;
    for (int i = 0; i < 16; i++) { band_a[i] = 0xFF; band_b[i] = 0x0F; }
    v16si band = band_a & band_b;
    if (band[0] != 0x0F) return 16;
    if (band[15] != 0x0F) return 17;

    v16si vd, ve;
    for (int i = 0; i < 16; i++) { vd[i] = 100; ve[i] = 9; }
    v16si vq = vd / ve;
    if (vq[0] != 11) return 18;
    v16si vm = vd % ve;
    if (vm[0] != 1) return 19;

    v16sf converted = __builtin_convertvector(vi, v16sf);
    if (converted[0] != 1.0f) return 20;
    if (converted[1] != -2.0f) return 21;

    // ---- long x8 ----
    v8di vl;
    for (int i = 0; i < 8; i++) vl[i] = 1000000000000LL + i;
    v8di vl2 = vl + vl;
    if (vl2[0] != 2000000000000LL) return 22;
    if (vl2[7] != 2000000000014LL) return 23;

    // ---- short x32 ----
    v32hi vh;
    for (int i = 0; i < 32; i++) vh[i] = (short)i;
    v32hi vh2 = vh + vh;
    if (vh2[5] != 10) return 24;
    if (vh2[31] != 62) return 25;

    // ---- char x64 ----
    v64qi vc;
    for (int i = 0; i < 64; i++) vc[i] = (char)i;
    v64qi vc2 = vc + vc;
    if (vc2[15] != 30) return 26;
    if ((unsigned char)vc2[63] != 126) return 27;

    // ---- by-value call ABI at this width ----
    v16sf arg = make16f(1.0f);
    float expect = 0.0f;
    for (int i = 0; i < 16; i++) expect += 1.0f + (float)i;
    if (sum16f(arg) != expect) return 28;

    return 42;
}
