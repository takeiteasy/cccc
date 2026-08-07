// CCCC_FLAGS: -3
// EXPECT_RUNTIME_ERROR
// Ticket #916 follow-up: with the bogus-address bug fixed (see
// test_discarded_deref_safety.c), a discarded-value dereference through a
// genuinely dangling pointer must now be caught as a dangling access by
// -3's per-frame liveness check, instead of the pre-fix behaviour of either
// segfaulting (float/double) or falsely/incidentally "trapping" against
// address 0 for the wrong reason (int).
int *f(void) {
    int x = 5;
    return &x; // address escapes; x's frame is dead once f() returns
}

int main(void) {
    int *p = f();
    *p; // discarded deref through a dangling pointer -- must trap
    return 42;
}
