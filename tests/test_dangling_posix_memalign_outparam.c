// CCCC_FLAGS: -3
// Ticket #669: the exact case hit while testing #668 -- posix_memalign(&q,
// ...) takes the address of a local `void *` out-param. Before the #669
// fix, this aborted with "DANGLING POINTER DETECTED" at scope exit purely
// because `&q` was tracked, even though the pointer never escapes main().
#include <stdlib.h>

int main(void) {
    void *q = NULL;
    int rc = posix_memalign(&q, 64, 128);
    if (rc != 0 || !q)
        return 1;
    free(q);
    return 42;
}
