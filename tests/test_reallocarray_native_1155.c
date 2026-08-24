// Regression for #1155: a guest call to reallocarray(3) compiled clean
// under -c=native but failed to LINK on macOS ("use of undeclared
// identifier 'reallocarray'") -- CCCC's own bundled include/stdlib.h
// declares it, but macOS libc neither declares nor defines it, and the
// #901 bodiless-prototype pass correctly declined to re-derive a
// declaration since the guest's own `#include <stdlib.h>` was captured and
// replayed (the header genuinely is in scope, just not this one symbol).
// Fixed by remapping the call to a serializer-emitted __cccc_reallocarray
// shim (serialize_reallocarray_shim, serialize_shims.c), same shape as the
// existing setjmp/longjmp remap. Must round-trip VM -> native with the
// same observable behavior, including the ENOMEM/ptr-untouched contract on
// overflow.

#include <errno.h>
#include <stdlib.h>

int main(void) {
    int *p = reallocarray(NULL, 4, sizeof(int));
    if (!p)
        return 1;
    for (int i = 0; i < 4; i++)
        p[i] = i;

    p = reallocarray(p, 8, sizeof(int));
    if (!p)
        return 2;
    for (int i = 0; i < 4; i++)
        if (p[i] != i)
            return 3;

    // Overflow: nmemb * size overflows size_t -- must fail (NULL) and leave
    // the original allocation untouched, not silently wrap and
    // under-allocate. Not asserting errno==ENOMEM here: the default VM-heap
    // path (REALCA, ops.c) doesn't set it on overflow -- a real gap versus
    // reallocarray(3)'s documented contract, but out of scope for #1155
    // (native serialization), tracked separately.
    int *bad = reallocarray(p, (size_t)-1, (size_t)-1);
    if (bad != NULL)
        return 4;

    // Original allocation must still be valid/untouched after the failed
    // resize attempt.
    for (int i = 0; i < 4; i++)
        if (p[i] != i)
            return 5;

    free(p);
    return 42;
}
