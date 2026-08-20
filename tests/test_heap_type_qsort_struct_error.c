// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #769: proves wrap_qsort's shadow-preservation is *element-pattern*
// uniformity, not plain byte-uniformity -- a struct array (mixed member
// types, TY_VOID padding between members) must still survive a qsort()
// call. Sorting an array of {int; double} structs, then reading the
// double member back as int, must still be flagged after the sort.
#include <stdlib.h>

struct S {
    int    a;
    double b;
};

static int cmp_s(const void *x, const void *y) {
    const struct S *sx = x, *sy = y;
    return (sx->a > sy->a) - (sx->a < sy->a);
}

int main(void) {
    struct S *arr = malloc(sizeof(struct S) * 4);
    for (int i = 0; i < 4; i++) {
        arr[i].a = 4 - i;
        arr[i].b =
            (double)(4 - i); // stamps each element's a (int) and b (double)
    }

    qsort(arr, 4, sizeof(struct S), cmp_s);

    int *p = (int *)&arr[0].b; // same bytes as arr[0].b, reinterpreted as int*
    int  result = *p; // load as int: mismatches the stamped double type
    free(arr);
    return result;
}
