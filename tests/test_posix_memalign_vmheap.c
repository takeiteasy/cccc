// Ticket #668: posix_memalign must be routed through the VM heap's
// alignment-aware bump allocator (PMEMA) so it gets an AllocHeader like
// malloc/calloc/realloc. Verify success returns 0 and an aligned pointer,
// invalid alignment returns EINVAL, and free() finds the header afterwards.
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    void *p = NULL;
    int rc = posix_memalign(&p, 64, 256);
    if (rc != 0)
        return 1;
    if (!p)
        return 2;
    if ((uintptr_t)p % 64 != 0)
        return 3;

    char *bytes = (char *)p;
    for (int j = 0; j < 256; j++)
        bytes[j] = (char)(j ^ 0x5A);
    for (int j = 0; j < 256; j++) {
        if (bytes[j] != (char)(j ^ 0x5A))
            return 4;
    }
    free(p);

    // Not a power of two -> EINVAL, memptr left untouched.
    void *q = (void *)0x1;
    rc = posix_memalign(&q, 3, 16);
    if (rc != EINVAL)
        return 5;

    // Not a multiple of sizeof(void*) -> EINVAL.
    rc = posix_memalign(&q, 4, 16); // 4 < sizeof(void*)=8 on this VM
    if (rc != EINVAL)
        return 6;

    return 42;
}
