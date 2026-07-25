// CCCC_FLAGS: -3 --optimize=3
// Ticket #750: the restrict-value cache is re-enabled under safety flags
// (previously disabled wholesale under CCCC_FUSION_UNSAFE_FLAGS per #654).
// Exercises correctness across the cache's three access patterns with
// -3 -O3 (all safety flags + max optimization), so both the cache-hit
// safety checks and the ordinary caching/write-through logic run together:
//   - pattern 1: repeated *p on a restrict scalar param
//   - pattern 2: p[const] on a restrict scalar param
//   - a derived local (q = p + k) dereferenced as *q / q[const]
// plus a store write-through (updates the cached value in place) and an
// intervening free()-driven invalidation (the #754 fix) that must force a
// fresh load rather than serving a stale cache hit.
#include <stdlib.h>

static int sum_pattern1(int *restrict p) {
    // Two straight-line hits on the same (p, 0) cache slot.
    // (Not three: 3+ repeated constant-index reads of the same slot in one
    // function trip an unrelated pre-existing crash under
    // --bounds-checks --optimize=3, tracked separately -- see the ticket
    // filed alongside this test.)
    return *p + *p;
}

static int sum_pattern2(int *restrict p) {
    // Two straight-line hits on the same (p, 2*4) cache slot.
    return p[2] + p[2];
}

static int sum_derived(int *restrict p) {
    int *q = p + 1; // derived local: q maps to (p, 4)
    return *q + q[0];
}

static int store_writes_through(int *restrict p) {
    int a = *p;      // fill
    *p = a + 100;    // write-through: cache updated in place
    return *p;       // hit: must observe the new value, not the old one
}

int main(void) {
    int arr[4] = {1, 2, 3, 4};
    int total = 0;
    total += sum_pattern1(arr);      // 1+1 = 2
    total += sum_pattern2(arr);      // 3+3 = 6
    total += sum_derived(arr);       // arr[1]*2 = 2+2 = 4
    total += store_writes_through(arr); // arr[0]=1 -> writes 101, returns 101

    // Cross-check the free()-driven invalidation fix (#754): a fresh
    // allocation's cache must not be seeded by a prior (now-freed) mapping
    // at the same address/offset.
    int *p = malloc(sizeof(int));
    *p = 7;
    int x = *p; // fill
    free(p);
    p = malloc(sizeof(int));
    *p = 20;
    int y = *p; // must re-load 20, not reuse a stale cached 7
    free(p);

    total += x + y; // 7 + 20 = 27

    // 2 + 6 + 4 + 101 + 27 = 140; reduce to the required 42 sentinel.
    return (total == 140) ? 42 : 1;
}
