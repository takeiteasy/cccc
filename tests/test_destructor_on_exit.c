// #680: __attribute__((destructor)) functions now also run on an explicit
// guest exit() call, not only on normal return from main() -- via wrap_exit's
// drain_destructors_nested (stdlib.c). Same ordering as
// test_destructor_priority.c's normal-return case: default group first,
// then descending priority.
#include <stdlib.h>
#include <unistd.h>

static int order[3];
static int order_idx;

__attribute__((destructor)) void ddefault(void) {
    order[order_idx++] = 0;
}
__attribute__((destructor(200))) void d200(void) {
    order[order_idx++] = 200;
}
// Runs last (highest priority runs first among prioritised ones, but here
// only d101/d200 are prioritised and d101 < d200 in priority number, so
// d101 -- the lowest-numbered priority -- runs last of the two, and after
// the default group). Verifies the full recorded order.
__attribute__((destructor(101))) void d101(void) {
    order[order_idx++] = 101;
    if (order_idx == 3 && order[0] == 0 && order[1] == 200 && order[2] == 101)
        _exit(42);
    _exit(1);
}

int main(void) {
    exit(0); // explicit guest exit() -- must still run destructors
}
