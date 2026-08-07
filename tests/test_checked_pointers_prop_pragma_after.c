// EXPECT_RUNTIME_ERROR
// #919's propagation is gated on --checked-pointers at parse time
// (propagate_checked_bounds() in src/parse.c registers/runs only when
// CCCC_CHECKED_BOUNDS is set), which raised the question of whether a
// #pragma cccc config(checked_pointers = true) appearing textually AFTER
// the declarations it should affect would fail to propagate them. It does
// not: #pragma cccc config(...) is resolved during preprocessing
// (src/preprocess.c), a pass that runs to completion for the whole
// translation unit before parse() ever sees a token, so vm->flags already
// has CCCC_CHECKED_BOUNDS set by the time any declaration is parsed
// regardless of where in the file the pragma is written. This is a
// regression test for that, not a demonstration of a limitation -- the
// out-of-bounds access through a propagated pointer traps even though the
// pragma comes after every declaration involved.

int main(void) {
    int n = 4;
    int * [[cccc::array, cccc::count(n)]] p = (int[4]){1, 2, 3, 4};
    int *q = p + 0;
    volatile int i = 4;
    int x = q[i];
    return x;
}

#pragma cccc config(checked_pointers = true)
