// Ticket #668: aligned_alloc must be routed through the VM heap's
// alignment-aware bump allocator (MALCA) so it gets an AllocHeader like
// malloc/calloc/realloc, instead of falling through to the host allocator
// via FFI. Verify the returned pointer is actually aligned for a range of
// alignments, that it is writable/readable, and that free() finds its
// header (falling into the MFRE VM-heap path rather than host free()).
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    size_t alignments[] = {8, 16, 32, 64, 256};
    for (int i = 0; i < 5; i++) {
        size_t alignment = alignments[i];
        void *p = aligned_alloc(alignment, 128);
        if (!p)
            return 1;
        if ((uintptr_t)p % alignment != 0)
            return 2;

        // Write/read the whole buffer to prove it's real, usable memory.
        char *bytes = (char *)p;
        for (int j = 0; j < 128; j++)
            bytes[j] = (char)j;
        for (int j = 0; j < 128; j++) {
            if (bytes[j] != (char)j)
                return 3;
        }

        free(p); // must find the AllocHeader via ((AllocHeader*)p)-1
    }
    return 42;
}
