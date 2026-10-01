// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: CCCC_LOCAL is only valid as a cccc_launch argument
#include <cccc/kernel.h>
int main(void) {
    int *p = CCCC_LOCAL(16);
    return p ? 42 : 1;
}
