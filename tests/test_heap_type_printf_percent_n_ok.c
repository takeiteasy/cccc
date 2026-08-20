// CCCC_FLAGS: --type-checks
// Ticket #768: a %n conversion can write through any pointer-shaped
// argument, not just a designated output buffer, so ffi_shadow_backstop
// must demote FFI_SHADOW_PRINTF back to the default whole-allocation clear
// whenever the format string may contain %n. Passing a stamped heap
// pointer alongside a %n argument, then reusing that allocation as another
// type, must not false-positive.
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    int *arr = malloc(sizeof(int) * 4);
    arr[0]   = 1;
    arr[1]   = 2;
    arr[2]   = 3;
    arr[3]   = 4; // stamps arr's whole range as int

    int n    = 0;
    // Format string contains %n: must fall back to the default
    // whole-allocation clear for every pointer-shaped argument, including
    // arr, exactly as before this classification existed.
    printf("%p%n\n", (void *)arr, &n);

    float *fbuf = (float *)arr;
    fbuf[0]     = 3.0f;
    int result  = (int)fbuf[0];
    free(arr);
    return (result == 3) ? 42 : 1;
}
