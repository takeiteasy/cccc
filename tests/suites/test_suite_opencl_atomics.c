// CCCC_FLAGS: --testing -x cl
// Suite: OpenCL 1.2 atomics on __global and __local memory. Every test checks
// an exact total across work-items, so a lost update fails it.

#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics : enable
#pragma OPENCL EXTENSION cl_khr_local_int32_base_atomics : enable

#pragma cccc suite begin "opencl_atomics"

__kernel void add_sub(__global int *sum, __global int *diff,
                      __global uint *usum) {
    size_t i = get_global_id(0);
    atomic_add(sum, (int)i);
    atomic_sub(diff, 1);
    atomic_add(usum, 2u);
}

[[cccc::test(return = 42)]]
int test_add_and_sub(void) {
    int  sum = 0, diff = 100;
    uint usum = 0;
    cccc_launch(add_sub, CCCC_RANGE(16), CCCC_RANGE(4), &sum, &diff, &usum);
    return sum == 120 && diff == 84 && usum == 32 ? 42 : 1;
}

__kernel void bits(__global int *and_, __global int *or_, __global int *xor_) {
    size_t i = get_global_id(0);
    atomic_and(and_, ~(1 << i));
    atomic_or(or_, 1 << i);
    atomic_xor(xor_, 3);
}

[[cccc::test(return = 42)]]
int test_bitwise(void) {
    int and_ = 0xff, or_ = 0, xor_ = 0;
    cccc_launch(bits, CCCC_RANGE(8), CCCC_RANGE(4), &and_, &or_, &xor_);
    // 8 xors of 3 cancel
    return and_ == 0 && or_ == 0xff && xor_ == 0 ? 42 : 1;
}

__kernel void inc_dec(__global int *up, __global int *down, __global int *old) {
    size_t i = get_global_id(0);
    atomic_inc(up);
    atomic_dec(down);
    atom_inc(up);
    old[i] = 0;
}

[[cccc::test(return = 42)]]
int test_increment_and_decrement(void) {
    int up = 0, down = 10, old[8];
    cccc_launch(inc_dec, CCCC_RANGE(8), CCCC_RANGE(4), &up, &down, old);
    return up == 16 && down == 2 ? 42 : 1;
}

// Each atomic returns the value before the operation, so the returned values
// are a permutation of 0..n-1
__kernel void ticket(__global int *counter, __global int *seen) {
    int mine = atomic_inc(counter);
    atomic_or(&seen[mine], 1);
}

[[cccc::test(return = 42)]]
int test_returns_old_value(void) {
    int counter = 0, seen[16] = {0};
    cccc_launch(ticket, CCCC_RANGE(16), CCCC_RANGE(4), &counter, seen);
    for (int i = 0; i < 16; i++)
        if (seen[i] != 1)
            return 1;
    return counter == 16 ? 42 : 1;
}

__kernel void extremes(__global int *mn, __global int *mx, __global uint *umx) {
    size_t i = get_global_id(0);
    atomic_min(mn, 100 - (int)i);
    atomic_max(mx, (int)i * 3 - 20);
    atomic_max(umx, (uint)i);
}

[[cccc::test(return = 42)]]
int test_min_and_max(void) {
    int  mn = 1000, mx = -1000;
    uint umx = 0;
    cccc_launch(extremes, CCCC_RANGE(16), CCCC_RANGE(4), &mn, &mx, &umx);
    return mn == 85 && mx == 25 && umx == 15 ? 42 : 1;
}

__kernel void cas(__global int *flag, __global int *old) {
    old[get_global_id(0)] = atomic_cmpxchg(flag, 0, (int)get_global_id(0) + 1);
}

[[cccc::test(return = 42)]]
int test_compare_exchange(void) {
    int flag = 0, old[8], winners = 0;
    cccc_launch(cas, CCCC_RANGE(8), CCCC_RANGE(4), &flag, old);
    for (int i = 0; i < 8; i++)
        winners += old[i] == 0;
    // one work-item saw 0 and set the flag; every other saw the winner's value
    return winners == 1 && flag >= 1 && flag <= 8 ? 42 : 1;
}

__kernel void exchange(__global int *slot, __global float *fslot,
                       __global int *old, __global float *fold) {
    size_t i = get_global_id(0);
    old[i]   = atomic_xchg(slot, (int)i + 1);
    fold[i]  = atomic_xchg(fslot, (float)i + 0.5f);
}

[[cccc::test(return = 42)]]
int test_exchange_int_and_float(void) {
    int   slot  = 0, old[8];
    float fslot = -1.0f, fold[8];
    cccc_launch(exchange, CCCC_RANGE(8), CCCC_RANGE(4), &slot, &fslot, old,
                fold);
    // the returned values plus the final one are the initial value and every
    // value written, each exactly once
    int   isum = slot;
    float fsum = fslot;
    for (int i = 0; i < 8; i++) {
        isum += old[i];
        fsum += fold[i];
    }
    return isum == 36 && fsum == 31.0f ? 42 : 1;
}

// Local-memory atomics: a histogram per work-group
__kernel void histogram(__global const uint *data, __global uint *bins) {
    __local uint local_bins[4];
    size_t       l = get_local_id(0);
    if (l < 4)
        local_bins[l] = 0;
    barrier(CLK_LOCAL_MEM_FENCE);
    atomic_inc(&local_bins[data[get_global_id(0)] % 4]);
    barrier(CLK_LOCAL_MEM_FENCE);
    if (l < 4)
        atomic_add(&bins[l], local_bins[l]);
}

[[cccc::test(return = 42)]]
int test_local_memory_histogram(void) {
    uint data[32], bins[4] = {0};
    for (int i = 0; i < 32; i++)
        data[i] = i * 7;
    cccc_launch(histogram, CCCC_RANGE(32), CCCC_RANGE(8), data, bins);
    return bins[0] + bins[1] + bins[2] + bins[3] == 32 && bins[0] == 8 &&
                   bins[1] == 8
               ? 42
               : 1;
}

__kernel void ulong_ops(__global ulong *total, __global long *signed_total) {
    atomic_add(total, 1ul << 40);
    atomic_add(signed_total, -3);
}

[[cccc::test(return = 42)]]
int test_long_atomics(void) {
    ulong total        = 0;
    long  signed_total = 0;
    cccc_launch(ulong_ops, CCCC_RANGE(4), CCCC_RANGE(2), &total, &signed_total);
    return total == (4ul << 40) && signed_total == -12 ? 42 : 1;
}

__kernel void long_ops(__global long *mn, __global ulong *mx,
                       __global long *flag, __global long *slot,
                       __global long *old) {
    size_t i = get_global_id(0);
    atomic_min(mn, -(long)i - 5);
    atomic_max(mx, (ulong)i << 33);
    old[i] = atomic_cmpxchg(flag, 0, (long)i + 1);
    atomic_xchg(slot, (long)i + 100);
}

[[cccc::test(return = 42)]]
int test_long_min_max_exchange(void) {
    long  mn = 0, flag = 0, slot = 0, old[8];
    ulong mx = 0;
    cccc_launch(long_ops, CCCC_RANGE(8), CCCC_RANGE(4), &mn, &mx, &flag, &slot,
                old);
    int winners = 0;
    for (int i = 0; i < 8; i++)
        winners += old[i] == 0;
    return mn == -12 && mx == (7ul << 33) && winners == 1 && flag >= 1 &&
                   slot >= 100 && slot <= 107
               ? 42
               : 1;
}

// OpenCL's own signatures take volatile pointers
__kernel void volatile_ops(volatile __global int *i, volatile __global float *f,
                           volatile __global int *old) {
    atomic_add(i, 1);
    old[get_global_id(0)] = (int)atomic_xchg(f, 2.5f);
}

[[cccc::test(return = 42)]]
int test_volatile_pointers(void) {
    int   i = 0, old[4];
    float f = 1.0f;
    cccc_launch(volatile_ops, CCCC_RANGE(4), CCCC_RANGE(2), &i, &f, old);
    return i == 4 && f == 2.5f ? 42 : 1;
}

#pragma cccc suite end
