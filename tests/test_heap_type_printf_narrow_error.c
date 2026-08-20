// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #768: printf is classified FFI_SHADOW_PRINTF. With no %n in the
// format string, printf writes through no pointer argument at all (out_arg
// == -1), so passing a stamped heap pointer purely to be printed (%p) must
// no longer clear its shadow -- type confusion on that allocation right
// after must still be caught. Before this classification, printf was
// unclassified (default whole-allocation clear), which would have erased
// the stamp and hidden the bug.
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    int *arr = malloc(sizeof(int) * 4);
    arr[0]   = 1;
    arr[1]   = 2;
    arr[2]   = 3;
    arr[3]   = 4; // stamps arr's whole range as int

    // printf receives the heap pointer itself (as %p); the format string
    // has no %n, so this must not clear arr's shadow at all.
    printf("%p\n", (void *)arr);

    float *tail = (float *)&arr[2];
    float  v    = *tail; // still stamped int: mismatch must be caught
    free(arr);
    return (int)v;
}
