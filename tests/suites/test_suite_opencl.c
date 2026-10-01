// CCCC_FLAGS: --testing -x cl
// Suite: OpenCL C kernels (qualifiers, work-item builtins, barriers, __local
// memory) launched from host code with cccc_launch. Vectors and atomics are in
// test_suite_opencl_vectors.c and test_suite_opencl_atomics.c; dialect
// selection is in tools/opencl_cli_smoke.py.

#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics : enable
#pragma OPENCL FP_CONTRACT ON

#pragma cccc suite begin "opencl"

__constant int scale_table[4] = {2, 3, 5, 7};

__kernel void vadd(__global int *out, __global const int *a,
                   __global const int *b) {
    size_t i = get_global_id(0);
    out[i]   = a[i] + b[i];
}

[[cccc::test(return = 42)]]
int test_vector_add(void) {
    int a[8] = {1, 2, 3, 4, 5, 6, 7, 8}, b[8] = {8, 7, 6, 5, 4, 3, 2, 1},
        out[8];
    cccc_launch(vadd, CCCC_RANGE(8), CCCC_RANGE(4), out, a, b);
    for (int i = 0; i < 8; i++)
        if (out[i] != 9)
            return 1;
    return 42;
}

kernel void scaled(global int *out) {
    out[get_global_id(0)] = scale_table[get_global_id(0) % 4];
}

[[cccc::test(return = 42)]]
int test_constant_table(void) {
    int out[8];
    cccc_launch(scaled, CCCC_RANGE(8), CCCC_RANGE(2), out);
    return out[0] == 2 && out[1] == 3 && out[2] == 5 && out[3] == 7 &&
                   out[4] == 2 && out[7] == 7
               ? 42
               : 1;
}

__kernel void ids_2d(__global int *out, int width) {
    size_t x = get_global_id(0), y = get_global_id(1);
    out[y * width + x] = (int)(get_group_id(0) * 1000 + get_group_id(1) * 100 +
                               get_local_id(0) * 10 + get_local_id(1));
    if (get_work_dim() != 2 || get_global_offset(0) != 0 ||
        get_global_offset(1) != 0 || get_global_size(0) != 6 ||
        get_local_size(1) != 2 || get_num_groups(0) != 2)
        out[y * width + x] = -1;
}

[[cccc::test(return = 42)]]
int test_ids_and_sizes_2d(void) {
    int out[6 * 4] = {0};
    cccc_launch(ids_2d, CCCC_RANGE(6, 4), CCCC_RANGE(3, 2), out, 6);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 6; x++) {
            int want = (x / 3) * 1000 + (y / 2) * 100 + (x % 3) * 10 + y % 2;
            if (out[y * 6 + x] != want)
                return 1;
        }
    return 42;
}

// A __local array in the kernel body and a barrier between work-items
__kernel void group_sum_array(__global const int *in, __global int *sums) {
    __local int tile[8];
    size_t      l = get_local_id(0);
    tile[l]       = in[get_global_id(0)];
    barrier(CLK_LOCAL_MEM_FENCE);
    for (size_t s = get_local_size(0) / 2; s > 0; s /= 2) {
        if (l < s)
            tile[l] += tile[l + s];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if (l == 0)
        sums[get_group_id(0)] = tile[0];
}

// The same reduction through a __local pointer parameter sized at launch
__kernel void group_sum_param(__global const int *in, __global int *sums,
                              __local int *tile) {
    size_t l = get_local_id(0);
    tile[l]  = in[get_global_id(0)];
    barrier(CLK_LOCAL_MEM_FENCE);
    for (size_t s = get_local_size(0) / 2; s > 0; s /= 2) {
        if (l < s)
            tile[l] += tile[l + s];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if (l == 0)
        sums[get_group_id(0)] = tile[0];
}

[[cccc::test(return = 42)]]
int test_local_array_reduction(void) {
    int in[16], sums[2];
    for (int i = 0; i < 16; i++)
        in[i] = i + 1;
    cccc_launch(group_sum_array, CCCC_RANGE(16), CCCC_RANGE(8), in, sums);
    return sums[0] == 36 && sums[1] == 100 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_local_pointer_parameter(void) {
    int in[16], sums[2];
    for (int i = 0; i < 16; i++)
        in[i] = i + 1;
    cccc_launch(group_sum_param, CCCC_RANGE(16), CCCC_RANGE(8), in, sums,
                CCCC_LOCAL(8 * sizeof(int)));
    return sums[0] == 36 && sums[1] == 100 ? 42 : 1;
}

// Helpers take OpenCL-qualified pointers; an unmarked helper parameter takes
// the space of its callers.
static int dot3(__global const int *a, __global const int *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void fill(__local int *tile, int v) {
    tile[get_local_id(0)] = v;
}

__kernel void uses_helpers(__global const int *a, __global const int *b,
                           __global int *out) {
    __local int tile[4];
    fill(tile, (int)get_local_id(0) + 1);
    barrier(CLK_LOCAL_MEM_FENCE);
    out[get_global_id(0)] = dot3(a, b) + tile[3 - get_local_id(0)];
}

[[cccc::test(return = 42)]]
int test_helper_functions(void) {
    int a[3] = {1, 2, 3}, b[3] = {4, 5, 6}, out[4];
    cccc_launch(uses_helpers, CCCC_RANGE(4), CCCC_RANGE(4), a, b, out);
    return out[0] == 32 + 4 && out[3] == 32 + 1 ? 42 : 1;
}

// Both spellings of every qualifier
__kernel void spellings(__global int *g, global int *h, __constant int *c,
                        constant int *d, __local int *l, local int *m,
                        __private int p, private int q) {
    __private int own  = p + q;
    l[get_local_id(0)] = c[0] + d[1];
    m[get_local_id(0)] = own;
    barrier(CLK_LOCAL_MEM_FENCE);
    g[0] = l[0];
    h[0] = m[0];
}

[[cccc::test(return = 42)]]
int test_both_qualifier_spellings(void) {
    int g = 0, h = 0, c[2] = {10, 20};
    cccc_launch(spellings, CCCC_RANGE(1), CCCC_RANGE(1), &g, &h, c, c,
                CCCC_LOCAL(sizeof(int)), CCCC_LOCAL(sizeof(int)), 3, 4);
    return g == 30 && h == 7 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_scalar_typedefs(void) {
    uchar  c = 255;
    ushort s = 65535;
    uint   i = 4294967295u;
    ulong  l = 18446744073709551615ul;
    return sizeof(uchar) == 1 && sizeof(ushort) == 2 && sizeof(uint) == 4 &&
                   sizeof(ulong) == 8 && c == 255 && s == 65535 &&
                   i == 4294967295u && l + 1 == 0
               ? 42
               : 1;
}

__attribute__((reqd_work_group_size(4, 1, 1))) __kernel void
hinted(__global int *out) {
    out[get_global_id(0)] = (int)get_local_size(0);
}

__kernel __attribute__((work_group_size_hint(4, 1, 1))) void
hinted2(__global int *out) {
    out[get_global_id(0)] = 2;
}

[[cccc::test(return = 42)]]
int test_work_group_attributes_accepted(void) {
    int out[4];
    cccc_launch(hinted, CCCC_RANGE(4), CCCC_RANGE(4), out);
    int ok = out[0] == 4;
    cccc_launch(hinted2, CCCC_RANGE(4), CCCC_RANGE(4), out);
    return ok && out[3] == 2 ? 42 : 1;
}

__kernel void fenced(__global int *out) {
    out[get_global_id(0)] = 1;
    mem_fence(CLK_GLOBAL_MEM_FENCE);
    read_mem_fence(CLK_LOCAL_MEM_FENCE);
    write_mem_fence(CLK_GLOBAL_MEM_FENCE | CLK_LOCAL_MEM_FENCE);
}

[[cccc::test(return = 42)]]
int test_memory_fences(void) {
    int out[4] = {0};
    cccc_launch(fenced, CCCC_RANGE(4), CCCC_RANGE(2), out);
    return out[0] + out[3] == 2 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_dialect_macros(void) {
    return __OPENCL_C_VERSION__ == 120 && CL_VERSION_1_2 == 120 &&
                   CLK_LOCAL_MEM_FENCE == CCCC_LOCAL_FENCE &&
                   CLK_GLOBAL_MEM_FENCE == CCCC_GLOBAL_FENCE
               ? 42
               : 1;
}

#pragma cccc suite end
