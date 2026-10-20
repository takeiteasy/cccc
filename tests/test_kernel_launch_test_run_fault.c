// CCCC_FLAGS: --test-run -o /dev/null
// A launch that faults in the --test-run smoke phase stops the native build.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: local size 3 does not divide global size 8
#include <cccc/kernel.h>

[[cccc::kernel]] static void k([[cccc::global]] int *out) {
    out[cccc_global_id(0)] = 1;
}

int main(void) {
    int out[8] = {0};
    cccc_launch(k, CCCC_RANGE(8), CCCC_RANGE(3), out);
    return 0;
}
