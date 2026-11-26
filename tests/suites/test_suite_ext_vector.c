// CCCC_FLAGS: --testing
// Suite: clang's ext_vector_type and the NEON vector attributes. An ext vector
// takes component selectors in C; sizes and results match clang.

#pragma cccc suite begin "ext_vector"

typedef float          f2 __attribute__((ext_vector_type(2)));
typedef float          f3 __attribute__((ext_vector_type(3)));
typedef float          f4 __attribute__((ext_vector_type(4)));
typedef float          f8 __attribute__((ext_vector_type(8)));
typedef int            i4 __attribute__((ext_vector_type(4)));
typedef double         d5 __attribute__((ext_vector_type(5)));
typedef char           c1 __attribute__((ext_vector_type(1)));
typedef char           c3 __attribute__((ext_vector_type(3)));
typedef float          gv4 __attribute__((vector_size(16)));
typedef int            n2 __attribute__((neon_vector_type(2)));
typedef short          n8 __attribute__((neon_vector_type(8)));
typedef unsigned char  p8 __attribute__((neon_polyvector_type(8)));
typedef unsigned short p8h __attribute__((neon_polyvector_type(8)));

[[cccc::test(return = 42)]]
int test_sizes(void) {
    return sizeof(f2) == 8 && sizeof(f3) == 16 && sizeof(f4) == 16 &&
                   sizeof(f8) == 32 && sizeof(d5) == 64 && sizeof(c1) == 1 &&
                   sizeof(c3) == 4 && _Alignof(f2) == 8 &&
                   _Alignof(f3) == 16 && _Alignof(f4) == 16
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_arithmetic_and_splat(void) {
    f4 a = {1, 2, 3, 4};
    f4 b = a + 1;
    f4 c = a * 2.0;
    f4 d = 10 - a;
    f4 e = (f4)(2.0f);
    return b.x == 2 && b.w == 5 && c.y == 4 && c.w == 8 && d.x == 9 &&
                   d.w == 6 && e.z == 2
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_comparison_mask(void) {
    f4 a = {1, 2, 3, 4};
    f4 b = {4, 2, 2, 4};
    i4 m = a < b;
    i4 n = a == b;
    return m.x == -1 && m.y == 0 && m.z == 0 && n.y == -1 && n.z == 0 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_swizzle_reads(void) {
    f4 a = {1, 2, 3, 4};
    f4 r = a.wzyx;
    f2 s = a.xy;
    f3 t = a.xyz;
    f4 u = a.rgba;
    f4 v = a.bgra;
    f2 w = a.s13;
    f2 lo = a.lo, hi = a.hi, ev = a.even, od = a.odd;
    return r.x == 4 && r.w == 1 && s.y == 2 && t.z == 3 && u.w == 4 &&
                   v.x == 3 && v.z == 1 && w.x == 2 && w.y == 4 && lo.y == 2 &&
                   hi.x == 3 && ev.y == 3 && od.x == 2 && a.s2 == 3 &&
                   a.S3 == 4 && a.b == 3
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_chained_swizzle(void) {
    f4 a = {1, 2, 3, 4};
    f4 b = a.xyzw.wzyx;
    f2 c = a.zw.yx;
    return b.x == 4 && b.w == 1 && c.x == 4 && c.y == 3 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_swizzle_stores(void) {
    f4 a = {1, 2, 3, 4};
    a.x  = 10;
    a.zw = a.xy;
    f2 p = {7, 8};
    a.xy = p.yx;
    return a.x == 8 && a.y == 7 && a.z == 10 && a.w == 2 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_three_lane_vector(void) {
    f3 v = {1, 2, 3};
    f3 w = v + v;
    f4 q = {5, 6, 7, 8};
    f3 t = q.xyz;
    return v.z == 3 && w.x == 2 && w.z == 6 && t.z == 7 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_odd_lane_counts(void) {
    d5 d = {1, 2, 3, 4, 5};
    c3 c = {1, 2, 3};
    c1 o = {7};
    return d.s4 == 5 && (d + d).s3 == 8 && c.z == 3 && o.x == 7 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_vector_size_interop(void) {
    gv4 g = {1, 2, 3, 4};
    f4  e = g;
    gv4 h = e + 1;
    return e.w == 4 && h[0] == 2 ? 42 : 1;
}

float __attribute__((ext_vector_type(4))) global_v = {1, 2, 3, 4};

[[cccc::test(return = 42)]]
int test_declarator_position(void) {
    float __attribute__((ext_vector_type(4))) local = global_v.wzyx;
    return local.x == 4 && sizeof(local) == 16 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_neon_sizes(void) {
    n2  a = {1, 2};
    n8  b = {1, 2, 3, 4, 5, 6, 7, 8};
    p8  c = {1, 2, 3, 4, 5, 6, 7, 8};
    p8h d = {1, 2, 3, 4, 5, 6, 7, 8};
    n2  e = a + a;
    return sizeof(n2) == 8 && sizeof(n8) == 16 && sizeof(p8) == 8 &&
                   sizeof(p8h) == 16 && e[1] == 4 && b[7] == 8 && c[3] == 4 &&
                   d[7] == 8
               ? 42
               : 1;
}

#pragma cccc suite end
