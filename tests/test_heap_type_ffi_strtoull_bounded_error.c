// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #839: strtoull was missing an FFI_SHADOW_BOUNDED entry in
// ffi_shadow_rules[] (src/ops.c), so its *endptr write fell through to the
// default whole-allocation clear instead of being narrowed to the
// statically-known sizeof(char*) it actually writes. Mirrors
// test_heap_type_ffi_bounded_error.c: stamp only the TAIL of a heap array
// as int, then have strtoull's endptr write land in the HEAD only -- the
// tail's int stamp must survive, so a later float load through it must
// still be caught.
#include <stdlib.h>

int main(void) {
    int *arr = malloc(sizeof(int) * 4);
    arr[2]   = 3;
    arr[3]   = 4; // stamps only arr[2..3] (the tail) as int

    // endptr write lands at &arr[0], a sizeof(char*) extent -- must clear
    // just the head, not the whole allocation.
    char **endptr = (char **)&arr[0];
    strtoull("123", endptr, 10);

    float *tail = (float *)&arr[2];
    float  v    = *tail; // load as float: mismatches the still-stamped int tail
    free(arr);
    return (int)v;
}
