// CCCC_FLAGS: --checked-pointers
// #488: the substitutability gate. A parameter's bounds template may
// reference another parameter (here, count(n) references `n`); if THAT
// call's actual argument for `n` is not side-effect-free, the caller-side
// check is silently declined rather than emitted -- emitting it would
// re-evaluate a side-effecting expression an extra time, which
// man/SAFETY.md already establishes is unacceptable for the identical
// reason on the member-bounds side (f()->p[i]). Uses
// node_has_side_effects(), not checked_obj_is_trivial() (which has no
// ND_NUM arm and would wrongly decline a bare integer literal -- see
// test_checked_call_arg_count_trap.c's own literal `8`).
//
// Two cases in one file: a call() argument, and an i++ argument. Both
// must compile and run clean -- no trap -- with any side effect having
// run exactly once.

#include <stdio.h>

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

static int calls = 0;
static int f(void) {
    calls++;
    return 8; // lies about small's real extent, same as the trap test --
              // but this argument is declined, so nothing ever checks it
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};

    sink(small, f());
    if (calls != 1)
        return 1;

    int i = 8;
    sink(small, i++);
    if (i != 9)
        return 1;

    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
