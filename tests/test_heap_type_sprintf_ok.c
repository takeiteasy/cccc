// CCCC_FLAGS: --type-checks
// Ticket #768: sprintf is classified FFI_SHADOW_PRINTF but has no length
// argument (len_arg == -1, fixed_len == 0), so its write extent isn't
// statically bounded -- ffi_shadow_backstop must fall through to the
// default whole-allocation clear for its own out_arg, exactly as an
// unclassified call would. Reusing the buffer as another type right after
// must not false-positive.
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    int *buf = malloc(sizeof(int) * 2);
    buf[0] = 1;
    buf[1] = 2; // stamps buf's whole range as int

    char *cbuf = (char *)buf;
    sprintf(cbuf, "ab"); // unbounded write through cbuf: must clear buf's
                          // whole allocation, not just narrow to a %n-style
                          // partial clear

    float *fbuf = (float *)buf;
    fbuf[0] = 3.0f;
    int result = (int)fbuf[0];
    free(buf);
    return (result == 3) ? 42 : 1;
}
