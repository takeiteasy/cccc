// CCCC_FLAGS: --type-checks --vm-profile
// CCCC_EXPECT_STDERR: shadow_pages_swept: [1-9]
// Ticket #767: type_shadow_fill's page reclaim rule only frees a page when
// a single clear zeroes it in full; a page zeroed via multiple *partial*
// clears (the edge pages of a multi-page freed allocation, the main case)
// used to stay allocated forever. Allocating across several pages, stamping
// every byte, then freeing exercises exactly that -- interior pages hit the
// existing full-page reclaim, but the tail page is only partially cleared
// (nothing beyond the allocation was ever stamped) and relies on
// type_shadow_sweep to reclaim it.
#include <stdlib.h>

int main(void) {
    size_t n = 48 * 1024; // ~192 KiB of ints: spans multiple 64 KiB shadow pages
    int *arr = malloc(n * sizeof(int));
    for (size_t i = 0; i < n; i++)
        arr[i] = (int)i; // stamps every byte in range as int

    free(arr); // clears the whole range; tail page only partially zeroed
    return 42;
}
