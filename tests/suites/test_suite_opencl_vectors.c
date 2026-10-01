// CCCC_FLAGS: --testing -x cl
// Suite: OpenCL C vector types, swizzles and vector literals. Vectors under 16
// bytes (float2, int2, char4, ...) do not exist, so a swizzle that would make
// one is rejected; see tests/test_opencl_*_error.c.

#pragma cccc suite begin "opencl_vectors"

[[cccc::test(return = 42)]]
int test_vector_sizes(void) {
    return sizeof(float3) == 16 && sizeof(float4) == 16 &&
                   sizeof(float8) == 32 && sizeof(float16) == 64 &&
                   sizeof(int3) == 16 && sizeof(int8) == 32 &&
                   sizeof(uint16) == 64 && sizeof(char16) == 16 &&
                   sizeof(uchar16) == 16 && sizeof(short8) == 16 &&
                   sizeof(ushort16) == 32 && sizeof(long2) == 16 &&
                   sizeof(long3) == 32 && sizeof(ulong8) == 64
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_arithmetic_and_broadcast(void) {
    float4 a = (float4)(1, 2, 3, 4);
    float4 b = a * 2.0f + (float4)(1.0f);
    int8   c = (int8)(1, 2, 3, 4, 5, 6, 7, 8);
    int8   d = c + 10;
    return b.x == 3 && b.w == 9 && d.s0 == 11 && d.s7 == 18 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_component_names(void) {
    float4 v = (float4)(10, 20, 30, 40);
    return v.x == 10 && v.y == 20 && v.z == 30 && v.w == 40 && v.s0 == 10 &&
                   v.s3 == 40 && v.S2 == 30
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_hex_lane_names(void) {
    float16 v = (float16)(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    return v.s9 == 9 && v.sA == 10 && v.sa == 10 && v.sF == 15 && v.sf == 15
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_swizzle_reads(void) {
    float4 v = (float4)(1, 2, 3, 4);
    float4 r = v.wzyx;
    float4 s = v.xxyy;
    float4 t = v.s3210;
    float3 u = v.xyz;
    float3 w = v.s123;
    return r.x == 4 && r.w == 1 && s.x == 1 && s.z == 2 && s.w == 2 &&
                   t.x == 4 && t.w == 1 && u.x == 1 && u.z == 3 && w.x == 2 &&
                   w.z == 4
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_halves_and_interleaves(void) {
    float8  v  = (float8)(0, 1, 2, 3, 4, 5, 6, 7);
    float4  lo = v.lo, hi = v.hi, ev = v.even, od = v.odd;
    float16 w = (float16)(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    float8  wl = w.lo, wh = w.hi;
    return lo.x == 0 && lo.w == 3 && hi.x == 4 && hi.w == 7 && ev.y == 2 &&
                   ev.w == 6 && od.x == 1 && od.w == 7 && wl.s7 == 7 &&
                   wh.s0 == 8 && wh.s7 == 15
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_single_lane_writes(void) {
    float4 v  = (float4)(0);
    v.x       = 1;
    v.s1      = 2;
    v.z      += 3;
    v.w       = v.x + v.s1;
    return v.x == 1 && v.y == 2 && v.z == 3 && v.w == 3 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_multi_lane_writes(void) {
    float8 v = (float8)(0, 1, 2, 3, 4, 5, 6, 7);
    v.lo     = (float4)(10, 11, 12, 13);
    v.odd    = (float4)(20, 21, 22, 23);
    float4 r = (v.hi = (float4)(1, 2, 3, 4));
    float3 t = (float3)(1, 2, 3);
    t.s210   = (float3)(7, 8, 9);
    float4 q = (float4)(0);
    q.wzyx   = (float4)(1, 2, 3, 4);
    // lo: 10 11 12 13, odd lanes (1 3 5 7): 20 21 22 23, hi: 1 2 3 4
    return v.s0 == 10 && v.s1 == 20 && v.s2 == 12 && v.s3 == 21 && v.s4 == 1 &&
                   v.s5 == 2 && v.s6 == 3 && v.s7 == 4 && r.w == 4 &&
                   t.x == 9 && t.z == 7 && q.x == 4 && q.w == 1
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_vector_literals(void) {
    float4 splat = (float4)(7);
    float4 four  = (float4)(1, 2, 3, 4);
    float3 three = (float3)(1, 2, 3);
    float8 cat   = (float8)(four, four.wzyx);
    float8 mixed = (float8)(four, 5, 6, 7, 8);
    float4 copy  = (float4)(four);
    int4   ints  = (int4)(1.5f, 2.5f, 3, 4);
    return splat.x == 7 && splat.w == 7 && four.w == 4 && three.z == 3 &&
                   cat.s0 == 1 && cat.s3 == 4 && cat.s4 == 4 && cat.s7 == 1 &&
                   mixed.s3 == 4 && mixed.s4 == 5 && mixed.s7 == 8 &&
                   copy.y == 2 && ints.x == 1 && ints.y == 2
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_literal_components_evaluated_once(void) {
    int  n = 0;
    int4 v = (int4)(n++, n++, n++, n++);
    int4 s = (int4)(n++);
    return v.x == 0 && v.y == 1 && v.w == 3 && s.x == 4 && s.w == 4 && n == 5
               ? 42
               : 1;
}

[[cccc::test(return = 42)]]
int test_integer_vectors(void) {
    uchar16 u = (uchar16)(1);
    short8  s = (short8)(1, 2, 3, 4, 5, 6, 7, 8);
    long2   l = (long2)(40, 2);
    uint4   q = (uint4)(1u, 2u, 3u, 4u);
    return u.sF == 1 && s.s7 == 8 && s.s0 == 1 && l.x + l.y == 42 &&
                   q.wzyx.x == 4
               ? 42
               : 1;
}

// Vectors in global memory: swizzles on a dereferenced __global pointer
__kernel void flip(__global float4 *out, __global const float4 *in) {
    size_t i = get_global_id(0);
    out[i]   = in[i].wzyx;
}

__kernel void halves(__global float8 *data) {
    size_t i   = get_global_id(0);
    float4 lo  = data[i].lo;
    data[i].hi = lo * 2.0f;
    data[i].s0 = 100;
}

[[cccc::test(return = 42)]]
int test_vectors_in_global_memory(void) {
    float4 in[2] = {{1, 2, 3, 4}, {5, 6, 7, 8}}, out[2];
    cccc_launch(flip, CCCC_RANGE(2), CCCC_RANGE(1), out, in);
    float8 d[1] = {{0, 1, 2, 3, 4, 5, 6, 7}};
    cccc_launch(halves, CCCC_RANGE(1), CCCC_RANGE(1), d);
    return out[0].x == 4 && out[0].w == 1 && out[1].x == 8 && out[1].w == 5 &&
                   d[0].s0 == 100 && d[0].s4 == 0 && d[0].s5 == 2 &&
                   d[0].s7 == 6
               ? 42
               : 1;
}

#pragma cccc suite end
