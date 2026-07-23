// 256-bit GNU vector_size support (tracker #722, follow-up to #715). Same
// operation coverage as the 128-bit tests (test_attr_vector_size*.c) but at
// double the width, across all six lane families, to exercise the
// operand-carried lane-count/byte-width path added by #722 (VReg widened to
// 64 bytes; every vector opcode now loops a runtime count instead of a
// literal). Also covers __builtin_convertvector and by-value call ABI at
// this width.

typedef float v8sf __attribute__((vector_size(32)));
typedef double v4df __attribute__((vector_size(32)));
typedef int v8si __attribute__((vector_size(32)));
typedef long v4di __attribute__((vector_size(32)));
typedef short v16hi __attribute__((vector_size(32)));
typedef char v32qi __attribute__((vector_size(32)));

static v8sf make8f(float base) {
    v8sf v;
    for (int i = 0; i < 8; i++) v[i] = base + (float)i;
    return v;
}

static float sum8f(v8sf v) {
    float s = 0.0f;
    for (int i = 0; i < 8; i++) s += v[i];
    return s;
}

int main(void) {
    // ---- float32 x8: arithmetic, negate, splat, compare, select ----
    v8sf a, b;
    for (int i = 0; i < 8; i++) { a[i] = (float)(i + 1); b[i] = (float)(10 * (i + 1)); }

    v8sf c = a + b;
    for (int i = 0; i < 8; i++)
        if (c[i] != (float)(i + 1) + (float)(10 * (i + 1))) return 1;

    v8sf d = b - a;
    if (d[0] != 9.0f) return 2;
    if (d[7] != 72.0f) return 3;

    v8sf e = a * 2.0f; // scalar broadcast (splat)
    if (e[0] != 2.0f) return 4;
    if (e[7] != 16.0f) return 5;

    v8sf f = b / a;
    for (int i = 0; i < 8; i++)
        if (f[i] != 10.0f) return 6;

    v8sf g = -a;
    if (g[0] != -1.0f) return 7;
    if (g[7] != -8.0f) return 8;

    v8si cmp = (a < (v8sf){5,5,5,5,5,5,5,5});
    v8sf sel = cmp ? a : b;
    if (sel[0] != 1.0f) return 9;   // a[0]=1 < 5 -> then-arm
    if (sel[7] != 80.0f) return 10; // a[7]=8 not< 5 -> else-arm

    // ---- double x4 ----
    v4df p, q;
    p[0] = 1.5; p[1] = 2.5; p[2] = 3.5; p[3] = 4.5;
    q[0] = 0.5; q[1] = 0.5; q[2] = 0.5; q[3] = 0.5;
    v4df r = p + q;
    if (r[0] != 2.0) return 11;
    if (r[3] != 5.0) return 12;

    // ---- int32 x8: arithmetic, bitwise, div/mod, convertvector ----
    v8si vi = {1, -2, 3, -4, 5, -6, 7, -8};
    v8si vi2 = vi * 3;
    if (vi2[0] != 3) return 13;
    if (vi2[7] != -24) return 14;
    v8si vi3 = -vi;
    if (vi3[1] != 2) return 15;

    v8si band = (v8si){0xF0,0x0F,0xFF,0x00,0xF0,0x0F,0xFF,0x00} &
                (v8si){0x0F,0xF0,0x0F,0xFF,0x0F,0xF0,0x0F,0xFF};
    if (band[0] != 0x00) return 16;
    if (band[2] != 0x0F) return 17;

    v8si vd = {10, -10, 7, -7, 100, -100, 50, -50};
    v8si ve = {3, 3, -3, -3, 9, 9, 6, 6};
    v8si vq = vd / ve;
    if (vq[0] != 3) return 18;
    if (vq[1] != -3) return 19;
    v8si vm = vd % ve;
    if (vm[0] != 1) return 20;

    v8sf converted = __builtin_convertvector(vi, v8sf);
    if (converted[0] != 1.0f) return 21;
    if (converted[1] != -2.0f) return 22;

    // ---- long x4 ----
    v4di vl = {1000000000000LL, -5, 42, -42};
    v4di vl2 = vl + vl;
    if (vl2[0] != 2000000000000LL) return 23;
    if (vl2[1] != -10) return 24;

    // ---- short x16 ----
    v16hi vh;
    for (int i = 0; i < 16; i++) vh[i] = (short)i;
    v16hi vh2 = vh + vh;
    if (vh2[5] != 10) return 25;
    if (vh2[15] != 30) return 26;

    // ---- char x32 ----
    v32qi vc;
    for (int i = 0; i < 32; i++) vc[i] = (char)i;
    v32qi vc2 = vc + vc;
    if (vc2[15] != 30) return 27;
    if ((unsigned char)vc2[31] != 62) return 28;

    // ---- brace-init / compound literal ----
    v8sf lit = (v8sf){1,2,3,4,5,6,7,8};
    if (lit[0] != 1.0f || lit[7] != 8.0f) return 29;

    // ---- by-value call ABI at this width ----
    v8sf arg = make8f(1.0f);
    if (sum8f(arg) != 1+2+3+4+5+6+7+8) return 30;

    return 42;
}
