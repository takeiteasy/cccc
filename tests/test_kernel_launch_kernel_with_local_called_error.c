// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: which declares local-memory objects
#include <cccc/kernel.h>
[[cccc::kernel]] static void inner([[cccc::global]] int *p) {
    [[cccc::local]] int tile[2];
    tile[0] = 1;
    p[0]    = tile[0];
}
[[cccc::kernel]] static void outer([[cccc::global]] int *p) {
    inner(p);
}
int main(void) {
    return 42;
}
