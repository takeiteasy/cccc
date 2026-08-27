// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// The VM has no bytecode optimiser (and no indexed-load fusion or
// restrict-value cache) any more, so every deref takes the checked path.
// This verifies a --type-checks build catches type confusion through a
// repeated restrict-pointer deref -- the shape #654's fusion bypass used to
// slip through -- via the ordinary CHKT3 emission.
#include <stdlib.h>

static int touch(int *restrict p) {
    int sum = 0;
    for (int i = 0; i < 2; i++)
        sum += *p; // pattern 1: repeated *p on a restrict scalar param
    return sum;
}

int main(void) {
    int *ip   = malloc(sizeof(int));
    *ip       = 5;
    float *fp = (float *)ip;
    *fp       = 1.0f; // re-stamps the allocation's effective type to float
    return touch((int *)fp); // load as int: mismatches the stamped float type
}
