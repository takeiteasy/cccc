// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #769: cc_type_shadow_elements_uniform must anchor its
// element-pattern comparison at the array *passed to qsort* (its `base`
// argument, arr+2 here), not at the containing allocation's start (arr).
// arr[0..1] are deliberately stamped a *different* type (float) from the
// uniformly-int sorted sub-range (arr[2..7]) so an implementation that
// wrongly widened the comparison window to include them would see a
// non-uniform pattern, clear the range, and let this test's error slip by
// -- a correct, arr+2-anchored implementation sees only the uniform int
// elements in the actual sorted range, preserves their shadow, and the
// type confusion on arr[3] (inside the sorted sub-range) must still be
// caught after the sort.
#include <stdlib.h>

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

int main(void) {
    int *arr          = malloc(sizeof(int) * 8);
    ((float *)arr)[0] = 1.0f;
    ((float *)arr)[1] =
        2.0f;           // arr[0..1]: stamped float, outside the sorted range
    for (int i = 2; i < 8; i++)
        arr[i] = 8 - i; // arr[2..7]: stamped int, uniformly

    qsort(arr + 2, 4, sizeof(int), cmp_int); // sorts only arr[2..5]

    float *fbuf = (float *)arr;
    float  v    = fbuf[3]; // load arr[3] (inside the sorted range) as float:
                           // mismatches int
    free(arr);
    return (int)v;
}
