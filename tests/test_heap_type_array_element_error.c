// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #653: byte-granular CHKT3 tracks effective type per array element
// offset too, not just the array's base pointer. Writing element [2] as
// int, then reading the same bytes through a float pointer, must be
// flagged even though element [0] was never touched.
#include <stdlib.h>

int main(void) {
    int *a = malloc(4 * sizeof(int));
    a[2] = 7; // stamps a[2]'s byte range (offset 8) as int
    float *fp = (float *)a;
    return (int)fp[2]; // load as float: mismatches the stamped int type
}
