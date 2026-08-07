// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #769: bsearch is classified FFI_SHADOW_READONLY -- the host
// bsearch() writes through no argument at all, and its comparator's guest
// code is fully shadow-tracked like any other guest execution via the
// same #738 trampoline qsort uses. A stamped array's shadow must survive a
// bsearch() call untouched, so the type confusion right after must still
// be caught.
#include <stdlib.h>

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

int main(void) {
    int *arr = malloc(sizeof(int) * 4);
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    arr[3] = 4; // stamps arr's whole range as int

    int key = 3;
    void *found = bsearch(&key, arr, 4, sizeof(int), cmp_int);
    (void)found;

    float *fbuf = (float *)arr;
    float v = fbuf[0]; // load as float: mismatches the still-stamped int type
    free(arr);
    return (int)v;
}
