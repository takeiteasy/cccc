// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #751: op_CALLF_fn's generic backstop used to clear an unshimmed
// host write's ENTIRE allocation, even when the call has a statically-
// known write extent -- destroying type coverage on bytes the call never
// touched. snprintf is classified FFI_SHADOW_BOUNDED (narrows the clear to
// its [buf, buf+n) argument), so stamping the TAIL of a heap array, then
// snprintf-ing into its HEAD, must leave the tail's shadow intact: type
// confusion on the untouched tail must still be caught.
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    int *arr = malloc(sizeof(int) * 4);
    arr[2] = 3;
    arr[3] = 4; // stamps only arr[2..3] (the tail) as int

    // Bounded write into the head only: must clear just [arr, arr+8), not
    // the whole allocation.
    snprintf((char *)arr, sizeof(int) * 2, "ab");

    float *tail = (float *)&arr[2];
    float v = *tail; // load as float: mismatches the still-stamped int tail
    free(arr);
    return (int)v;
}
