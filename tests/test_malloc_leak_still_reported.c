// CCCC_FLAGS: -1
// CCCC_EXPECT_STDOUT: MEMORY LEAK DETECTED
#include <stdlib.h>
int main(void) {
    void *p = malloc(8);
    (void)p;
    return 42;
}
