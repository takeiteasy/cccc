// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #753: the byte-granular type shadow is a sparse page table, not a
// flat array -- a page is freed back to host memory once a clear zeroes it
// in full (e.g. MFRE on a whole allocation), so a large-alloc-then-free
// pattern reclaims its shadow pages rather than paying for them for the
// rest of the process. Detection must survive that reclaim: allocate and
// free a buffer big enough to span several shadow pages, then allocate a
// fresh small buffer and confirm type confusion is still caught on it.
#include <stdlib.h>

int main(void) {
    // Several times the shadow page size (64 KiB) worth of int elements,
    // so freeing it exercises the page-reclaim path across multiple pages.
    size_t n   = (256 * 1024) / sizeof(int);
    int   *big = malloc(n * sizeof(int));
    for (size_t i = 0; i < n; i++)
        big[i] = (int)i; // stamps the whole range as int
    free(big);

    int *p   = malloc(sizeof(int));
    *p       = 5;
    float *q = (float *)p; // same base address, reinterpreted
    return (int)*q;        // load as float: mismatches the stamped int type
}
