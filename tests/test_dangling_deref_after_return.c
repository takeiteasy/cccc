// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --dangling-pointers
// Ticket #670: the -3 dangling-pointer check was neutered by #669 because
// the old scope-exit check had zero true-positive capability (it conflated
// address-taken with escaped). This is the real, precise replacement: a
// dereference-time range check in CHKP3 (the pointer-check gate). p holds
// &x from a function that has already returned, so by the time *p executes
// here in main(), x's slot is below the current stack pointer (the frame
// was reclaimed) -- this must abort.
int *get_local(void) {
    int x = 42;
    return &x;
}

int main(void) {
    int *p = get_local();
    return *p;
}
