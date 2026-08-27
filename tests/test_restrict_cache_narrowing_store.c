// The VM has no bytecode optimiser or restrict-value cache any more; this now
// pins that the narrowing store-through-restrict shape below still computes
// correct results through the ordinary codegen path.
// Historical context:
// Ticket #757: the restrict-value cache's write-through path
// (restrict_cache_handle_store, src/codegen.c) fully replaced the cached
// slot's contents and normalized to the *param's* declared pointee type,
// with no notion of a partial-width store. A store through a
// differently-typed pointer expression at the same restrict param/offset
// (narrower OR wider than the param's declared type) silently corrupted the
// cached value instead of either merging the correct bytes or invalidating.
// Fix: restrict_cache_store_type_matches() gates the write-through on the
// store's own pointee type matching the cache's tracked type; a mismatch
// invalidates the whole param instead.
#include <string.h>

// Narrowing: store a single byte through a char* into an int-typed cache slot.
static int narrowing(int *restrict p) {
    int a      = *p;   // cache fill: reads all 4 bytes as int
    *(char *)p = 0x7F; // narrowing store: only byte 0 should change
    int b      = *p;   // cache hit path must not serve the stale value
    return (a == 0x11223344 && b == 0x1122337f) ? 1 : 0;
}

// Widening: store a full int through an int* into a short-typed cache slot,
// spanning into the next short's cached bytes too.
static int widening(short *restrict p) {
    int a     = *p;         // fills the (p, 0) short-typed entry
    *(int *)p = 0x11223344; // widening store spans (p, 0) AND (p, 2)
    int b     = (unsigned short)p[0];
    int c     = (unsigned short)p[1];
    return (a == 0x0102 && b == 0x3344 && c == 0x1122) ? 1 : 0;
}

// Same-type store still uses the fast write-through path (not invalidation).
static int same_type(int *restrict p) {
    int a = *p;
    *p    = a + 100;
    return *p; // must observe the new value via write-through, not a reload
}

int main(void) {
    int x = 0x11223344;
    if (!narrowing(&x))
        return 1;

    short buf[2] = {0x0102, 0x0304};
    if (!widening(buf))
        return 2;
    if (buf[0] != 0x3344 || buf[1] != 0x1122)
        return 3;

    int y = 1;
    if (same_type(&y) != 101)
        return 4;

    return 42;
}
