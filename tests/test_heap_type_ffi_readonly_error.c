// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #751: op_CALLF_fn's generic backstop used to clear the shadow of
// any tracked heap allocation reachable through ANY host call, including a
// call that only ever reads the buffer -- destroying real type-confusion
// coverage for free. strlen/memcmp are classified FFI_SHADOW_READONLY, so
// passing a stamped buffer to them must no longer clear its shadow: the
// type confusion right after must still be caught.
#include <stdlib.h>
#include <string.h>

int main(void) {
    int *buf = malloc(sizeof(int) * 4);
    buf[0] = 1;
    buf[1] = 2;
    buf[2] = 3;
    buf[3] = 4; // stamps buf's whole range as int

    // Read-only host calls: must not clear buf's shadow.
    char *cbuf = (char *)buf;
    size_t len = strlen(cbuf); // reads bytes, writes nothing
    int cmp = memcmp(buf, buf, sizeof(int) * 4);

    float *fbuf = (float *)buf;
    float v = fbuf[0]; // load as float: mismatches the still-stamped int type
    free(buf);
    return (int)v + (int)len + cmp;
}
