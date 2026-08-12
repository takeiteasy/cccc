// CCCC_FLAGS: -2
// #982 (defect A): a pointer DIFFERENCE (`(p+8) - p`) was mis-checked by
// CHKB as if its operand were a scaled byte offset -- but new_sub's
// ptr-ptr arm hands CHKB the *subtrahend's own heap address* there, not a
// scaled offset, so `base_off + <heap address>` blew past the allocation's
// size and tripped a false ARRAY BOUNDS ERROR on every ptr-ptr subtraction
// against heap memory. This is a general CHKB bug, not VLA-specific --
// verified here against a plain malloc block (see tests/suites/
// test_suite_vla.c for the VLA-shaped repro that surfaced it, ticket #982).
// Fixed by excluding a pointer difference (identified by the ND_SUB node's
// own result type, not a pointer) from CHKB/CHKBN emission entirely.
#include <stdlib.h>
int main(void) {
    char *p = malloc(16);
    if (!p)
        return 1;
    long d = (p + 8) - p;
    free(p);
    return d == 8 ? 42 : 1;
}
