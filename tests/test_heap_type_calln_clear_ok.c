// CCCC_FLAGS: --type-checks
// Ticket #768: op_CALLN_fn (indirect calls through a function pointer or
// dlsym'd symbol) previously had no FFI-clear backstop at all -- an
// unclassified/unshimmed host function reached this way could write heap
// bytes with no VM-level hook and leave a stale shadow stamp behind,
// causing a FALSE POSITIVE on the next legitimate access as the buffer's
// new type. strcpy called indirectly through a function pointer routes
// through CALLN, not CALLF; its write must now clear the shadow the same
// way CALLF's backstop always has.
#include <stdlib.h>
#include <string.h>

int main(void) {
    int *buf                            = malloc(sizeof(int) * 2);
    buf[0]                              = 1;
    buf[1]                              = 2; // stamps buf's whole range as int

    char *(*scpy)(char *, const char *) = strcpy;
    char *cbuf                          = (char *)buf;
    scpy(cbuf, "ab"); // writes through CALLN with no VM-level hook

    float *fbuf = (float *)buf;
    fbuf[0]     = 3.0f;
    int result  = (int)fbuf[0];
    free(buf);
    return (result == 3) ? 42 : 1;
}
