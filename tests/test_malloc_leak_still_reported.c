// CCCC_FLAGS: -1
// CCCC_EXPECT_STDOUT: MEMORY LEAK DETECTED
// CCCC_EXPECT_LEAK: deliberate -- proves #979's fix (excluding alloca/VLA/__block automatic storage from the leak walk) didn't silently disable leak detection for genuine user allocations.
#include <stdlib.h>
int main(void) {
    void *p = malloc(8);
    (void)p;
    return 42;
}
