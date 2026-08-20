// GNU __attribute__((vector_size(N))) vector extension (tracker #72).
// Covers: parsing, lane read/write via subscript (constant and runtime
// index), element-wise +,-,*,/, unary negate, scalar broadcast (splat),
// vector-to-vector assignment, and mixed float/double/int lane types.
// Brace-initializer syntax and compound literals are covered separately in
// test_attr_vector_size_brace_init.c, test_attr_vector_size_brace_global.c,
// and test_attr_vector_size_compound_literal.c.

typedef float  v4sf __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef int    v4si __attribute__((vector_size(16)));
typedef long   v2di __attribute__((vector_size(16)));
typedef short  v8hi __attribute__((vector_size(16)));
typedef char   v16qi __attribute__((vector_size(16)));

// Vectors here are always constructed in place via per-lane assignment
// (this test covers arithmetic, not the call ABI); by-value function
// args/returns are covered separately in test_attr_vector_size_byval.c
// (tracker #714).

int main(void) {
    // Basic element-wise add/sub/mul/div on float32x4.
    v4sf a;
    a[0] = 1.0f;
    a[1] = 2.0f;
    a[2] = 3.0f;
    a[3] = 4.0f;
    v4sf b;
    b[0]   = 10.0f;
    b[1]   = 20.0f;
    b[2]   = 30.0f;
    b[3]   = 40.0f;

    v4sf c = a + b;
    if (c[0] != 11.0f)
        return 1;
    if (c[1] != 22.0f)
        return 2;
    if (c[2] != 33.0f)
        return 3;
    if (c[3] != 44.0f)
        return 4;

    v4sf d = b - a;
    if (d[0] != 9.0f)
        return 5;
    if (d[3] != 36.0f)
        return 6;

    v4sf e = a * 2.0f; // scalar broadcast (splat)
    if (e[0] != 2.0f)
        return 7;
    if (e[2] != 6.0f)
        return 8;

    v4sf f = b / a;
    if (f[0] != 10.0f)
        return 9;
    if (f[3] != 10.0f)
        return 10;

    // Unary negate.
    v4sf g = -a;
    if (g[0] != -1.0f)
        return 11;
    if (g[3] != -4.0f)
        return 12;

    // Runtime (non-constant) subscript index.
    int idx = 2;
    if (c[idx] != 33.0f)
        return 13;
    idx    = 3;
    c[idx] = 100.0f;
    if (c[3] != 100.0f)
        return 14;

    // Vector-to-vector assignment (copy).
    v4sf h;
    h = a;
    if (h[1] != 2.0f)
        return 15;

    // double x2 lanes.
    v2df p, q;
    p[0]   = 1.5;
    p[1]   = 2.5;
    q[0]   = 0.5;
    q[1]   = 0.5;
    v2df r = p + q;
    if (r[0] != 2.0)
        return 16;
    if (r[1] != 3.0)
        return 17;

    // int32 x4 lanes.
    v4si vi;
    vi[0]    = 1;
    vi[1]    = -2;
    vi[2]    = 3;
    vi[3]    = -4;
    v4si vi2 = vi * 3;
    if (vi2[0] != 3)
        return 18;
    if (vi2[1] != -6)
        return 19;
    v4si vi3 = -vi;
    if (vi3[2] != -3)
        return 20;

    // long x2 lanes.
    v2di vl;
    vl[0]    = 1000000000000LL;
    vl[1]    = -5;
    v2di vl2 = vl + vl;
    if (vl2[0] != 2000000000000LL)
        return 21;
    if (vl2[1] != -10)
        return 22;

    // short x8 lanes.
    v8hi vh;
    for (int i = 0; i < 8; i++)
        vh[i] = (short)i;
    v8hi vh2 = vh + vh;
    if (vh2[5] != 10)
        return 23;

    // char x16 lanes.
    v16qi vc;
    for (int i = 0; i < 16; i++)
        vc[i] = (char)i;
    v16qi vc2 = vc + vc;
    if (vc2[15] != 30)
        return 24;
    if ((unsigned char)vc2[15] != 30)
        return 25;

    return 42;
}
