// #680: a destructor that itself calls exit() must not recurse into the
// destructor drain again -- wrap_exit (stdlib.c) sets vm->dtors_drained
// *before* running the loop, so the re-entrant wrap_exit call sees the flag
// already set and skips straight to the real host exit(). If the drain were
// re-entered, d would run twice and re_entries would be 2, not 1.
#include <stdlib.h>
#include <unistd.h>

static int re_entries;

__attribute__((destructor)) void d(void) {
    re_entries++;
    if (re_entries > 1)
        _exit(1); // ran more than once -- recursion bug
    exit(re_entries == 1 ? 42 : 1); // re-enter wrap_exit from inside the destructor
}

int main(void) {
    exit(0);
}
