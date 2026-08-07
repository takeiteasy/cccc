// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --type-checks
// Ticket #914: the data-segment narrowing clear must stay narrow -- a
// bounded write into the HEAD of a global array must not touch the TAIL's
// shadow. Mirrors test_heap_type_ffi_bounded_error.c for the heap case.
#include <stdio.h>

static int garr[4];

int main(void) {
    garr[2] = 3;
    garr[3] = 4; // stamps only garr[2..3] (the tail) as int

    // Bounded write into the head only: must clear just [garr, garr+8),
    // leaving the tail's int stamp intact.
    snprintf((char *)garr, sizeof(int) * 2, "ab");

    float *tail = (float *)&garr[2];
    float v = *tail; // load as float: mismatches the still-stamped int tail
    return (int)v;
}
