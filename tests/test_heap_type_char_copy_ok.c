// CCCC_FLAGS: --type-checks
// Ticket #653: any object's representation is always legally accessible
// as character type (C11 6.5p7), so a hand-rolled byte-copy loop through
// char* must neither be flagged itself nor leave the destination's shadow
// mis-stamped as "char" -- op_CHKT3_fn's char-store special case clears
// the range instead of stamping it. A later, ordinary int read through the
// copied buffer must not false-positive.
#include <stdlib.h>

int main(void) {
    int *src = malloc(sizeof(int));
    *src = 99; // stamps src's effective type as int

    int *dst = malloc(sizeof(int));
    char *cs = (char *)src;
    char *cd = (char *)dst;
    for (unsigned long i = 0; i < sizeof(int); i++)
        cd[i] = cs[i]; // hand-rolled byte copy: char stores, char loads

    int result = *dst; // ordinary int read: must not false-positive
    free(src);
    free(dst);
    return result == 99 ? 42 : 1;
}
