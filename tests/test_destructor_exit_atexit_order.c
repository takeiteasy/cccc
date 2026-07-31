// #680: an explicit guest exit() must produce the same observable order as
// normal return from main() -- atexit handlers (LIFO) drain fully before any
// destructor runs (see cc_run in vm.c: cc_run_atexit_entries() runs before
// cc_run_init_entries(dtor_list), and wrap_exit, stdlib.c, mirrors that same
// order). Verified against a real host `cc` build first.
#include <stdlib.h>
#include <unistd.h>

static int order[3];
static int order_idx;

static void a1(void) { order[order_idx++] = 1; }
static void a2(void) { order[order_idx++] = 2; }

// The only destructor -- must run after both atexit handlers.
__attribute__((destructor)) void d(void) {
    order[order_idx++] = 3;
    if (order_idx == 3 && order[0] == 2 && order[1] == 1 && order[2] == 3)
        _exit(42);
    _exit(1);
}

int main(void) {
    atexit(a1); // registered first -- LIFO means it runs second
    atexit(a2); // registered second -- runs first
    exit(0);
}
