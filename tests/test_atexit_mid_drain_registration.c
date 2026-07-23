// #738 regression: a handler that itself calls atexit() while running must
// have the newly-registered handler also run before termination (verified
// against real glibc). cc_run_atexit_entries (vm.c) re-derefs
// vm->atexit_handlers/atexit_count fresh every loop iteration rather than
// capturing them once, both because a mid-drain registration can grow
// (realloc) the array out from under a stale pointer, and so the
// freshly-pushed handler becomes the new top of the LIFO list and runs
// next.
#include <stdlib.h>
#include <unistd.h>

static int h_late_ran;

static void h_late(void) {
    h_late_ran = 1;
    _exit(h_late_ran == 1 ? 42 : 1);
}

static void h1(void) {
    atexit(h_late); // registered WHILE atexit is already draining
    // If h_late never runs, main's own `return 0` exits with code 0.
}

int main(void) {
    atexit(h1);
    return 0;
}
