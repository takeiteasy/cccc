// CCCC_FLAGS: --test-run=max -o /dev/null
// CCCC_EXPECT_STDOUT: ^launch ok$
// CCCC_REJECT_STDOUT: mismatch
// The --test-run VM smoke phase runs a cccc_launch before the native build;
// it ignores main's exit code, so the program reports through stdout.
#include <stdio.h>
#include <cccc/kernel.h>

[[cccc::kernel]] static void fill([[cccc::global]] int *out) {
    out[cccc_global_id(0)] = (int)cccc_global_id(0) * 2;
}

[[cccc::kernel]] static void reverse([[cccc::global]] int *out) {
    [[cccc::local]] int tile[4];
    size_t              l = cccc_local_id(0);
    tile[l]               = (int)cccc_global_id(0);
    cccc_barrier(CCCC_LOCAL_FENCE);
    out[cccc_global_id(0)] = tile[3 - l];
}

[[cccc::kernel]] static void with_local([[cccc::global]] int *out,
                                        [[cccc::local]] int  *scratch) {
    size_t l   = cccc_local_id(0);
    scratch[l] = (int)l + 1;
    cccc_barrier(CCCC_LOCAL_FENCE);
    out[cccc_global_id(0)] = scratch[(l + 1) % 4];
}

[[cccc::kernel]] static void grid([[cccc::global]] int *out) {
    out[cccc_global_id(1) * 4 + cccc_global_id(0)] =
        (int)(cccc_group_id(1) * 10 + cccc_group_id(0));
}

int main(void) {
    int a[8] = {0};
    cccc_launch(fill, CCCC_RANGE(8), CCCC_RANGE(2), a);
    if (a[0] != 0 || a[3] != 6 || a[7] != 14)
        return printf("mismatch %d\n", 1);

    int b[8] = {0};
    cccc_launch(reverse, CCCC_RANGE(8), CCCC_RANGE(4), b);
    if (b[0] != 3 || b[3] != 0 || b[4] != 7 || b[7] != 4)
        return printf("mismatch %d\n", 2);

    int c[8] = {0};
    cccc_launch(with_local, CCCC_RANGE(8), CCCC_RANGE(4), c,
                CCCC_LOCAL(4 * sizeof(int)));
    if (c[0] != 2 || c[3] != 1 || c[4] != 2 || c[7] != 1)
        return printf("mismatch %d\n", 3);

    int d[16] = {0};
    cccc_launch(grid, CCCC_RANGE(4, 4), CCCC_RANGE(2, 2), d);
    if (d[0] != 0 || d[2] != 1 || d[8] != 10 || d[10] != 11)
        return printf("mismatch %d\n", 4);
    puts("launch ok");
    return 42;
}
