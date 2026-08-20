// CCCC_FLAGS: --safety=max
// CCCC_REJECT_STDOUT: MEMORY LEAK DETECTED
// #1009: a real allocation handed back through a `void **out` parameter and
// then genuinely freed must not report as leaked. #1009 turned out to be
// entirely downstream of #1008: the false UNINITIALIZED VARIABLE READ on
// the out-parameter locals aborted the program before `free` ever ran, and
// teardown then (correctly, given the abort) reported the still-live
// allocation as unfreed. Fixing #1008's read-side CHKI exemption for
// address-taken locals removes this leak report too, with no leak-detection
// change of its own needed. Exit code alone can't see this symptom -- both
// the trap and the leak report exit non-42/print regardless.
#include <stdlib.h>

static void mk(void **out, int *found) {
    *out   = malloc(40);
    *found = 1;
}

int main(void) {
    void *p;
    int   found;
    mk(&p, &found);
    if (!found)
        return 1;
    free(p);
    return 42;
}
