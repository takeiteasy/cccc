// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a \[\[cccc::local\]\] object cannot have an initializer
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    [[cccc::local]] int tile[2] = {1, 2};
    p[0]                        = tile[0];
}
int main(void) {
    return 42;
}
