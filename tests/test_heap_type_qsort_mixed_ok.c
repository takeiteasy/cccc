// CCCC_FLAGS: --type-checks
// Ticket #769: an array whose elements do *not* carry a uniform shadow
// pattern (three stamped int, one stamped float) must fall back to
// wrap_qsort's clear of [base, base+nmemb*size) -- reading any element as a
// type that mismatches its *pre-sort* stamp must not false-positive
// afterwards, since the clear (not a re-stamping store) is what makes the
// read safe.
#include <stdlib.h>

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

int main(void) {
    int *arr = malloc(sizeof(int) * 4);
    arr[0] = 4; // int
    arr[1] = 3; // int
    ((float *)arr)[2] = 2.0f; // float: breaks element-pattern uniformity
    arr[3] = 1; // int

    qsort(arr, 4, sizeof(int), cmp_int); // mixed shadow: falls back to the clear

    // No intervening stores: if the clear ran, every element reads back as
    // TY_VOID (no effective type) and any load is legal, including one
    // that mismatches what was stamped before the sort. If the clear had
    // NOT run, at least one of these loads would hit a stale stamp from
    // before qsort permuted the elements and error out.
    float *fbuf = (float *)arr;
    int *ibuf = arr;
    volatile float f0 = fbuf[0]; // was stamped int
    volatile float f1 = fbuf[1]; // was stamped int
    volatile int   i2 = ibuf[2]; // was stamped float
    volatile float f3 = fbuf[3]; // was stamped int
    (void)f0; (void)f1; (void)i2; (void)f3;

    free(arr);
    return 42;
}
