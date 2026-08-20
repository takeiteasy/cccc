// CCCC_FLAGS: --type-checks
// Ticket #653: a heap buffer's effective type must never go stale because
// of a write we can't observe -- a host FFI function (anything other than
// the shadow-aware memcpy/memmove shims) writing into a tracked heap
// allocation clears that allocation's shadow entirely (op_CALLF_fn's
// backstop, ops.c), rather than leaving behind whatever type was stamped
// before the call. Reusing the buffer as a different type right after must
// not false-positive.
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    int *buf = malloc(sizeof(int));
    *buf     = 5; // stamps buf's effective type as int

    // snprintf writes through buf's bytes with no VM-level hook: the
    // backstop must clear buf's shadow before this call runs.
    char *cbuf = (char *)buf;
    snprintf(cbuf, sizeof(int), "%c%c%c", 'a', 'b', '\0');

    // Reinterpret as float and write/read: must not false-positive against
    // a stale "int" stamp from before the snprintf call.
    float *fbuf = (float *)buf;
    *fbuf       = 3.0f;
    int result  = (int)*fbuf;
    free(buf);
    return (result == 3) ? 42 : 1;
}
