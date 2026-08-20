// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks --optimize=3
// Ticket #654: emit_indexed_load_if_possible/restrict-value-cache fusion
// paths bypass emit_load's CHKT3 emission at opt_level >= 2 and were only
// disabled when CCCC_POINTER_CHECKS | CCCC_INVALID_ARITH |
// CCCC_PROVENANCE_TRACK was set -- CCCC_TYPE_CHECKS wasn't in that mask.
// A standalone --type-checks -O3 build must still catch type confusion
// through a repeated restrict-pointer deref (restrict-cache pattern 1),
// which is exactly the path that used to slip through.
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
