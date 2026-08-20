// CCCC_FLAGS: --type-checks
// Ticket #769: a comparator is guest code, reentered synchronously through
// the #738 callback trampoline, so nothing stops it from writing through
// its `const void *` arguments mid-sort (unusual, but legal C -- the
// pointers aren't actually const-qualified data, just declared that way).
//
// The array starts non-uniformly typed (arr[1] is stamped float against an
// otherwise-int array), so wrap_qsort's *pre*-call check already fails and
// pre-clears the range before the host qsort() call runs at all -- this
// specifically exercises the pre-clear branch (as opposed to
// test_heap_type_qsort_mixed_ok.c's "no comparator writes" case). The
// comparator itself then also writes through one of its arguments,
// re-establishing a fresh (and, from the shadow's perspective, live) stamp
// on top of the pre-clear. wrap_qsort's post-call check must still run
// after this -- unconditionally, not gated on what the pre-call check
// found -- or that fresh stamp could survive at a stale position once
// qsort's untracked host-side swaps relocate the actual bytes elsewhere,
// and a later read through it would incorrectly resolve against a type the
// bytes there no longer represent. No intervening stores after the sort,
// so a clean run here depends entirely on wrap_qsort's own bookkeeping.
#include <stdlib.h>

static int g_calls;

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    if (g_calls++ == 0)
        *(float *)a = 9.0f; // mid-sort write: stamps this position float
    return (ia > ib) - (ia < ib);
}

int main(void) {
    int *arr          = malloc(sizeof(int) * 4);
    arr[0]            = 4;    // int
    ((float *)arr)[1] = 1.0f; // float: array is non-uniform going in
    arr[2]            = 2;    // int
    arr[3]            = 1;    // int

    qsort(arr, 4, sizeof(int), cmp_int);

    // No intervening stores. Shadow entries are keyed by address, not by
    // which logical array element currently lives there -- qsort's host
    // swaps relocate *values* between addresses without our tracking, but
    // never relocate the shadow itself. So whichever address the
    // comparator's mid-sort write landed on (position-dependent on the
    // host libc's qsort algorithm, not something this test pins down)
    // keeps a float stamp at that fixed address unless wrap_qsort's
    // post-call check re-clears it. Reading every index as int -- the
    // array's nominal type -- must be legal at all four: if the post-check
    // didn't run (the bug this test guards against), whichever index
    // received the write still carries a stale float stamp and this
    // mismatches, however qsort permuted the data underneath it.
    volatile int i0 = arr[0];
    volatile int i1 = arr[1];
    volatile int i2 = arr[2];
    volatile int i3 = arr[3];
    (void)i0;
    (void)i1;
    (void)i2;
    (void)i3;

    free(arr);
    return 42;
}
