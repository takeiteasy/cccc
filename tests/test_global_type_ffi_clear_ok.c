// CCCC_FLAGS: --type-checks
// Ticket #914: a global (file-scope/static) buffer's effective type must
// never go stale because of a write we can't observe, same as a heap
// buffer (see test_heap_type_ffi_clear_ok.c). strcpy is unclassified
// (FFI_SHADOW_DEFAULT), so ffi_shadow_backstop's whole-object clear must
// resolve a data-segment pointer too, not just heap_alloc_for_ptr -- a load
// through gbuf as a different type right after must not false-positive
// against the pre-call "int" stamp.
#include <string.h>

static int gbuf;

int main(void) {
    gbuf = 5; // stamps gbuf's effective type as int

    // strcpy writes through gbuf's bytes with no VM-level hook: the
    // backstop must clear gbuf's shadow before this call runs.
    strcpy((char *)&gbuf, "ab");

    // Load (no intervening store) as float: if the pre-call "int" stamp
    // survived, this mismatches and aborts; if the clear ran, the shadow
    // reads back TY_VOID and the load is unchecked.
    float *f = (float *)&gbuf;
    float v = *f;
    return (v == v) ? 42 : 1; // always true; just needs to reach here
}
