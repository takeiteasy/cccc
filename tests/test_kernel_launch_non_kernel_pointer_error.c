// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_RUNTIME_ERROR
// CCCC_EXPECT_STDERR: that is not a \[\[cccc::kernel\]\]
#include <cccc/kernel.h>
static void plain(int *p) {
    p[0] = 1;
}
int main(void) {
    int x[1];
    void (*fn)(int *) = plain;
    cccc_launch(fn, CCCC_RANGE(1), CCCC_RANGE(1), x);
    return 42;
}
