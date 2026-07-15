// CCCC_FLAGS: --type-checks
// Ticket #651: a matching type on store and load through a heap base
// pointer must never raise CHKT3's type mismatch, even with type checks
// enabled.
#include <stdlib.h>

int main(void) {
    int *p = malloc(sizeof(int));
    *p = 42;
    int result = *p;
    free(p);
    return result;
}
