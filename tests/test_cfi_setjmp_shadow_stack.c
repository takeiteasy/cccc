// JCC_FLAGS: -C
// CFI regression: longjmp must restore shadow_sp so the caller of the setjmp
// function can return without consuming orphaned shadow entries.
#include "setjmp.h"
#include "stdio.h"

static jmp_buf env;

static void level2(void) {
    longjmp(env, 1);
}

static void level1(void) {
    level2();
}

// setjmp and longjmp happen inside this function; when it returns, shadow_sp
// must match what it was on entry, not be 2 levels deeper.
static int do_work(void) {
    if (setjmp(env) == 0)
        level1();
    // After longjmp: shadow_sp has 2 orphaned entries from level1/level2 CALLs
    // unless SETJMP saved and LONGJMP restored shadow_sp.
    return 42;
}

int main(void) {
    return do_work();
    // When do_work returns, RETURN pops shadow_sp.
    // Without the fix: pops ret_after_level2_call instead of ret_after_do_work_call -> CFI violation.
    // With the fix: shadow_sp was restored, pops ret_after_do_work_call correctly.
}
