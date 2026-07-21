// CCCC_FLAGS: -3
// Ticket #673: longjmp can unwind several frames at once, which must retire
// every activation's liveness epoch above the target frame in one step
// (frame_epoch_truncate_to in op_LONGJMP_fn), not just the top one the way
// LEV3 does. If truncation is wrong, either a stale epoch lingers as "live"
// (a real bug goes uncaught) or the live set desyncs and a still-live local
// looks dangling (a false positive -- exactly what #669 got wrong). This is
// a benign, deeply-nested setjmp/longjmp program: it must run to completion.
#include <setjmp.h>

jmp_buf env;

void level3(int n) {
    int local3 = n * 3;
    (void)local3;
    longjmp(env, n + 1); // unwinds level3 + level2 + level1 in one jump
}

void level2(int n) {
    int local2 = n * 2;
    (void)local2;
    level3(n);
}

void level1(int n) {
    int local1 = n;
    (void)local1;
    level2(n);
}

int main(void) {
    int result = setjmp(env);
    if (result == 0) {
        level1(41);
        return 1; // unreachable
    }
    // Back in main after the multi-frame unwind. main's own locals (result)
    // must still be perfectly usable -- proves live_epochs wasn't corrupted.
    int check = result;
    return check == 42 ? 42 : 1;
}
