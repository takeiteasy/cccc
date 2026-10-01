// A launch with a local object and a barrier gives the same result in the VM
// and under -c=native (the native round-trip suite compiles every test).
#include <cccc/kernel.h>

[[cccc::kernel]] static void k([[cccc::global]] int *out) {
    [[cccc::local]] int tile[4];
    size_t              l = cccc_local_id(0);
    tile[l]               = (int)cccc_global_id(0);
    cccc_barrier(CCCC_LOCAL_FENCE);
    out[cccc_global_id(0)] = tile[3 - l];
}

int main(void) {
    int out[8] = {0};
    cccc_launch(k, CCCC_RANGE(8), CCCC_RANGE(4), out);
    return out[0] == 3 && out[3] == 0 && out[4] == 7 && out[7] == 4 ? 42 : 1;
}
