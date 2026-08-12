// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #839: strtof was missing an FFI_SHADOW_BOUNDED entry in
// ffi_shadow_rules[] (src/ops.c). Same shape as
// test_heap_type_ffi_strtoull_bounded_error.c, but strtof is registered as
// a direct host function pointer (not a cccc_* shim) with returns_double=2
// -- this also confirms float_arg_mask doesn't mark strtof's char** endptr
// argument as float-shaped and make ffi_shadow_backstop skip it.
#include <stdlib.h>

int main(void) {
    int *arr = malloc(sizeof(int) * 4);
    arr[2] = 3;
    arr[3] = 4; // stamps only arr[2..3] (the tail) as int

    // endptr write lands at &arr[0], a sizeof(char*) extent -- must clear
    // just the head, not the whole allocation.
    char **endptr = (char **)&arr[0];
    strtof("1.5", endptr);

    float *tail = (float *)&arr[2];
    float v = *tail; // load as float: mismatches the still-stamped int tail
    free(arr);
    return (int)v;
}
