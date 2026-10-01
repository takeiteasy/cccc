// CCCC_FLAGS: --testing
// Suite: cccc_launch and the work-item builtins from <cccc/kernel.h>. Negative
// cases live in tests/test_kernel_launch_*_error.c.

#include <cccc/kernel.h>

typedef struct {
    int x;
    int y;
} Pair;

#pragma cccc suite begin "kernel_launch"

[[cccc::kernel]] static void ids_1d([[cccc::global]] int *out) {
    size_t g       = cccc_global_id(0);
    out[g * 6 + 0] = (int)cccc_global_id(0);
    out[g * 6 + 1] = (int)cccc_local_id(0);
    out[g * 6 + 2] = (int)cccc_group_id(0);
    out[g * 6 + 3] = (int)cccc_global_size(0);
    out[g * 6 + 4] = (int)cccc_local_size(0);
    out[g * 6 + 5] = (int)cccc_num_groups(0) + (int)cccc_work_dim() * 100;
}

[[cccc::test(return = 42)]]
int test_ids_1d(void) {
    int out[8 * 6] = {0};
    cccc_launch(ids_1d, CCCC_RANGE(8), CCCC_RANGE(4), out);
    for (int g = 0; g < 8; g++) {
        int *r = &out[g * 6];
        if (r[0] != g || r[1] != g % 4 || r[2] != g / 4 || r[3] != 8 ||
            r[4] != 4 || r[5] != 2 + 100)
            return 1;
    }
    return 42;
}

[[cccc::kernel]] static void ids_2d([[cccc::global]] int *out, int width) {
    size_t x = cccc_global_id(0), y = cccc_global_id(1);
    out[y * width + x] =
        (int)(cccc_group_id(0) * 1000 + cccc_group_id(1) * 100 +
              cccc_local_id(0) * 10 + cccc_local_id(1));
    if (cccc_global_id(2) != 0 || cccc_local_size(2) != 1 ||
        cccc_num_groups(2) != 1)
        out[y * width + x] = -1;
}

[[cccc::test(return = 42)]]
int test_ids_2d(void) {
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

[[cccc::kernel]] static void ids_3d([[cccc::global]] int *out) {
    size_t i =
        (cccc_global_id(2) * 4 + cccc_global_id(1)) * 4 + cccc_global_id(0);
    out[i] =
        (int)(cccc_local_id(0) + cccc_local_id(1) * 2 + cccc_local_id(2) * 4);
    if (cccc_work_dim() != 3 || cccc_num_groups(0) != 2 ||
        cccc_num_groups(1) != 2 || cccc_num_groups(2) != 2)
        out[i] = -1;
}

[[cccc::test(return = 42)]]
int test_ids_3d(void) {
    int out[4 * 4 * 4] = {0};
    cccc_launch(ids_3d, CCCC_RANGE(4, 4, 4), CCCC_RANGE(2, 2, 2), out);
    for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                if (out[(z * 4 + y) * 4 + x] !=
                    x % 2 + (y % 2) * 2 + (z % 2) * 4)
                    return 1;
    return 42;
}

[[cccc::kernel]] static void saxpy([[cccc::global]] float       *y,
                                   [[cccc::global]] const float *x, float a,
                                   float bias, unsigned char n, long long k) {
    size_t i = cccc_global_id(0);
    if (i < n)
        y[i] = a * x[i] + bias + (float)k;
}

[[cccc::test(return = 42)]]
int test_mixed_scalar_args(void) {
    float x[4] = {1, 2, 3, 4}, y[4] = {0};
    cccc_launch(saxpy, CCCC_RANGE(4), CCCC_RANGE(2), y, x, 2.5f, 0.5f, 4, 10LL);
    return y[0] == 13.0f && y[1] == 15.5f && y[3] == 20.5f ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_int_literal_converts_to_float_param(void) {
    float x[2] = {1, 2}, y[2] = {0};
    cccc_launch(saxpy, CCCC_RANGE(2), CCCC_RANGE(2), y, x, 2, 1, 2, 0);
    return y[0] == 3.0f && y[1] == 5.0f ? 42 : 1;
}

[[cccc::kernel]] static void bump([[cccc::global]] int *out) {
    out[cccc_global_id(0)] += 1;
}

typedef [[cccc::kernel]] void KernelFn([[cccc::global]] int *);

[[cccc::test(return = 42)]]
int test_launch_through_function_pointer(void) {
    int       out[4] = {0};
    KernelFn *k      = bump;
    cccc_launch(k, CCCC_RANGE(4), CCCC_RANGE(2), out);
    cccc_launch(&bump, CCCC_RANGE(4), CCCC_RANGE(4), out);
    return out[0] == 2 && out[3] == 2 ? 42 : 1;
}

[[cccc::kernel]] static void group_reduce([[cccc::global]] const int *in,
                                          [[cccc::global]] int       *sums) {
    [[cccc::local]] int tile[64];
    [[cccc::local]] int count;
    size_t              l = cccc_local_id(0), n = cccc_local_size(0);
    if (l == 0)
        count = 0;
    tile[l] = in[cccc_global_id(0)];
    cccc_barrier(CCCC_LOCAL_FENCE);
    for (size_t s = n / 2; s > 0; s /= 2) {
        if (l < s)
            tile[l] += tile[l + s];
        cccc_barrier(CCCC_LOCAL_FENCE);
    }
    if (l == 0) {
        count                  = (int)n;
        sums[cccc_group_id(0)] = tile[0] + count;
    }
}

[[cccc::test(return = 42)]]
int test_reduction_through_local_memory(void) {
    int in[128], sums[2] = {0, 0};
    for (int i = 0; i < 128; i++)
        in[i] = i;
    cccc_launch(group_reduce, CCCC_RANGE(128), CCCC_RANGE(64), in, sums);
    return sums[0] == 63 * 64 / 2 + 64 &&
                   sums[1] == 127 * 128 / 2 - 63 * 64 / 2 + 64
               ? 42
               : 1;
}

[[cccc::kernel]] static void reverse_in_group([[cccc::global]] int *data,
                                              [[cccc::local]] int  *scratch) {
    size_t l = cccc_local_id(0), n = cccc_local_size(0);
    size_t base = cccc_group_id(0) * n;
    scratch[l]  = data[base + l];
    cccc_barrier(CCCC_LOCAL_FENCE | CCCC_GLOBAL_FENCE);
    data[base + l] = scratch[n - 1 - l];
}

[[cccc::test(return = 42)]]
int test_local_pointer_parameter(void) {
    int data[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    cccc_launch(reverse_in_group, CCCC_RANGE(8), CCCC_RANGE(4), data,
                CCCC_LOCAL(4 * sizeof(int)));
    return data[0] == 3 && data[3] == 0 && data[4] == 7 && data[7] == 4 ? 42
                                                                        : 1;
}

static inline void barrier_helper(void) {
    cccc_barrier(CCCC_LOCAL_FENCE);
}

[[cccc::kernel]] static void barrier_in_helper([[cccc::global]] int *flag,
                                               [[cccc::global]] int *out) {
    if (cccc_local_id(0) == 0)
        flag[0] = 7;
    barrier_helper();
    out[cccc_global_id(0)] = flag[0];
}

[[cccc::test(return = 42)]]
int test_barrier_reached_through_a_helper(void) {
    int flag[1] = {0}, out[4] = {0};
    cccc_launch(barrier_in_helper, CCCC_RANGE(4), CCCC_RANGE(4), flag, out);
    return out[0] == 7 && out[3] == 7 ? 42 : 1;
}

[[cccc::kernel]] static void diverge([[cccc::global]] int *out) {
    size_t l = cccc_local_id(0);
    if (l == 0)
        return;
    cccc_barrier(CCCC_LOCAL_FENCE);
    out[l] = 1;
}

[[cccc::test(return = 42)]]
int test_returning_item_does_not_deadlock_the_barrier(void) {
    int out[4] = {0};
    cccc_launch(diverge, CCCC_RANGE(4), CCCC_RANGE(4), out);
    return out[1] == 1 && out[3] == 1 ? 42 : 1;
}

[[cccc::kernel]] static int direct([[cccc::global]] int *o) {
    [[cccc::local]] int tile[2];
    tile[1] = 40;
    cccc_barrier(CCCC_LOCAL_FENCE);
    o[0] = (int)(cccc_global_id(0) + cccc_global_size(0) + cccc_local_size(0) +
                 cccc_num_groups(0) + cccc_work_dim());
    return tile[1];
}

[[cccc::test(return = 42)]]
int test_direct_call_is_a_single_work_item(void) {
    int o = 0;
    int t = direct(&o);
    return t == 40 && o == 0 + 1 + 1 + 1 + 1 ? 42 : 1;
}

[[cccc::test(return = 42)]]
int test_range_from_expression(void) {
    int        out[4] = {0};
    size_t     n      = 4;
    cccc_range g      = CCCC_RANGE(n);
    cccc_launch(bump, g, ((cccc_range){1, {2}}), out);
    return out[0] == 1 && out[3] == 1 ? 42 : 1;
}

[[cccc::kernel]] static void late_kernel([[cccc::global]] int *out);

[[cccc::test(return = 42)]]
int test_kernel_defined_after_the_launch(void) {
    int out[4] = {0};
    cccc_launch(late_kernel, CCCC_RANGE(4), CCCC_RANGE(2), out);
    return out[0] == 10 && out[3] == 13 ? 42 : 1;
}

[[cccc::kernel]] static void late_kernel([[cccc::global]] int *out) {
    out[cccc_global_id(0)] = 10 + (int)cccc_global_id(0);
}

[[cccc::test(return = 42)]]
int test_launches_in_a_loop(void) {
    int out[4] = {0};
    for (int i = 0; i < 5; i++)
        cccc_launch(bump, CCCC_RANGE(4), CCCC_RANGE(2), out);
    return out[0] == 5 && out[3] == 5 ? 42 : 1;
}

[[cccc::kernel]] static void zeroed_local([[cccc::global]] int *out) {
    [[cccc::local]] int seen[2];
    size_t              l         = cccc_local_id(0);
    out[cccc_group_id(0) * 2 + l] = seen[l];
    seen[l]                       = 99;
}

[[cccc::test(return = 42)]]
int test_local_memory_starts_zeroed_in_every_group(void) {
    int out[64] = {0};
    for (int i = 0; i < 64; i++)
        out[i] = -1;
    cccc_launch(zeroed_local, CCCC_RANGE(64), CCCC_RANGE(2), out);
    for (int i = 0; i < 64; i++)
        if (out[i] != 0)
            return 1;
    return 42;
}

[[cccc::kernel]] static void both_locals([[cccc::global]] int *out,
                                         [[cccc::local]] int  *scratch) {
    [[cccc::local]] int fixed[4];
    size_t              l = cccc_local_id(0), n = cccc_local_size(0);
    fixed[l]   = (int)l;
    scratch[l] = (int)(l * 10);
    cccc_barrier(CCCC_LOCAL_FENCE);
    out[cccc_global_id(0)] = fixed[n - 1 - l] + scratch[n - 1 - l];
}

[[cccc::test(return = 42)]]
int test_object_and_parameter_local_memory_do_not_overlap(void) {
    int out[8] = {0};
    cccc_launch(both_locals, CCCC_RANGE(8), CCCC_RANGE(4), out,
                CCCC_LOCAL(4 * sizeof(int)));
    return out[0] == 33 && out[3] == 0 && out[4] == 33 && out[7] == 0 ? 42 : 1;
}

[[cccc::kernel]] static void second_barrier_group([[cccc::global]] int *out) {
    [[cccc::local]] int shared;
    if (cccc_local_id(0) == 0)
        shared = (int)cccc_group_id(0) + 1;
    cccc_barrier(CCCC_LOCAL_FENCE);
    out[cccc_global_id(0)] = shared;
}

[[cccc::test(return = 42)]]
int test_every_barrier_group_gets_its_own_local_memory(void) {
    int out[12] = {0};
    cccc_launch(second_barrier_group, CCCC_RANGE(12), CCCC_RANGE(3), out);
    return out[0] == 1 && out[3] == 2 && out[6] == 3 && out[11] == 4 ? 42 : 1;
}

#pragma cccc suite end
