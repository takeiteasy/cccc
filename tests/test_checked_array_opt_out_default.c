// #487: checked-array typing (the extent becoming part of the type) is
// always on, but CHKR itself is opt-in (#770/#484): without
// --checked-pointers, the exact same out-of-bounds access that traps in
// test_checked_array_oob_trap.c must run clean here and return 42. A read,
// not a write, matching test_checked_pointers_opt_out_default.c's own
// convention -- a real -c=native binary has no VM bump-allocator slack past
// a stack array, so an out-of-bounds *write* here is genuine undefined
// behavior that can corrupt an adjacent stack canary/return address (it did,
// confirmed: SIGABRT under -c=native), not just an untrapped VM read.
int main(void) {
    int a        _Checked[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    volatile int i            = 10; // one past the declared extent -- no check
    int          x            = a[i];
    (void)x;
    return 42;
}
