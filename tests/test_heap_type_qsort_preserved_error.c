// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #769: qsort() on a heap array used to force a whole-allocation
// shadow clear (an unclassified FFI call), losing all type-confusion
// coverage for the array. wrap_qsort now preserves the shadow across the
// sort when every element carries the same shadow byte pattern going in --
// a plain permutation of a uniformly int-typed array can't change what
// CHKT3 catches. The type confusion right after the sort must still be
// caught.
#include <stdlib.h>

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

int main(void) {
    int *arr = malloc(sizeof(int) * 4);
    arr[0]   = 4;
    arr[1]   = 1;
    arr[2]   = 3;
    arr[3]   = 2; // stamps arr's whole range as int

    qsort(arr, 4, sizeof(int), cmp_int);

    float *fbuf = (float *)arr;
    float  v = fbuf[0]; // load as float: mismatches the still-stamped int type
    free(arr);
    return (int)v;
}
