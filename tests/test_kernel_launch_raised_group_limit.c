// CCCC_FLAGS: --kernel-max-group-size=512
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[cccc_global_id(0)] = 1;
}
int main(void) {
    int x[512] = {0};
    cccc_launch(k, CCCC_RANGE(512), CCCC_RANGE(512), x);
    int sum = 0;
    for (int i = 0; i < 512; i++)
        sum += x[i];
    return sum == 512 ? 42 : 1;
}
