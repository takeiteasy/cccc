// Failure path of the `__*_chk` natives (the VM side of `__builtin___*_chk`).
#include "../cccc.h"
#include "../internal.h"
#include <stdio.h>
#include <stdlib.h>

// Matches libc's __chk_fail: SIGABRT after a one-line diagnostic.
void cccc_fortify_fail(const char *fn) {
    fflush(stdout);
    fprintf(stderr, "*** buffer overflow detected ***: %s\n", fn);
    fflush(stderr);
    abort();
}
