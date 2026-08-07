// CCCC_FLAGS: --type-checks
// Ticket #914: FFI_SHADOW_BOUNDED's narrowing clear (ffi_shadow_backstop's
// out_arg branch, src/ops.c) must resolve a data-segment pointer via
// ffi_shadow_clear_extent too, not just heap_alloc_for_ptr -- otherwise a
// bounded write into the HEAD of a global array leaves its stale "int"
// stamp on the head, and a later load through the head as a different type
// (no intervening store) false-positives. snprintf is classified
// FFI_SHADOW_PRINTF/BOUNDED (narrows the clear to its [buf, buf+n)
// argument).
#include <stdio.h>

static int garr[2];

int main(void) {
    garr[0] = 1;
    garr[1] = 2; // stamps garr[0..1] as int

    // Bounded write covering the whole array: must clear [garr, garr+8).
    snprintf((char *)garr, sizeof(garr), "ab");

    // Load (no intervening store) as float: must not false-positive
    // against the pre-call "int" stamp.
    float *head = (float *)&garr[0];
    float v = *head;
    return (v == v) ? 42 : 1; // always true; just needs to reach here
}
