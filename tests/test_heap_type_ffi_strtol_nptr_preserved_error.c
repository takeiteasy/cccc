// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #839 follow-up: strtol's nptr argument (arg 0) is never written by
// the call, but a plain FFI_SHADOW_BOUNDED rule only narrows the clear for
// its designated out_arg (endptr) -- every *other* pointer-shaped argument,
// including nptr, still took the default whole-object clear. This wiped
// type-shadow coverage on bytes the call never touched whenever nptr shared
// an allocation with tracked data (a numeric string embedded in a larger
// heap buffer). other_args_readonly=true (src/ops.c) now skips the clear
// for nptr entirely, so a type stamp elsewhere in the same allocation must
// survive the call.
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *buf = malloc(64);
    memcpy(buf, "123", 4); // valid numeric string at the head

    int *tail = (int *)(buf + 32);
    *tail = 42; // stamps buf[32..35] as int, well outside strtol's endptr write

    char *endptr;
    strtol(buf, &endptr, 10); // arg0 (nptr) == buf: same allocation as tail

    float *ftail = (float *)(buf + 32);
    float v = *ftail; // load as float: mismatches the still-stamped int tail
    free(buf);
    return (int)v;
}
