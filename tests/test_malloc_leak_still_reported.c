// CCCC_EXPECT_LEAK: deliberate -- the alloca/VLA/__block exclusion from the leak walk must not hide a genuine user allocation
// CCCC_NATIVE_SKIP: VM-only leak detection
// CCCC_FLAGS: -1
// CCCC_EXPECT_STDOUT: MEMORY LEAK DETECTED
#include <stdlib.h>
int main(void) {
    void *p = malloc(8);
    (void)p;
    return 42;
}
