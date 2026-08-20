// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --dangling-pointers
// Ticket #675: the follow-up to #673's frame-epoch liveness check. &arr[i]
// with a runtime index compiles to a base LEA3 (arr's address) plus a
// separate runtime ADD -- the *final* dereferenced address is never itself
// recorded in stack_ptr_epochs, so a deeper-call deref through it used to
// fall back to #670's plain range check alone. `use`'s own frame reserves
// as much stack as get_local's (the `pad` array) so it re-covers the same
// memory arr occupied, defeating the range check (ptr >= sp again there) --
// exactly the same shape as #673's own deeper-call gap, but for an interior
// pointer. STKTAG (emitted after arr's base LEA3, since &arr[i] escapes via
// the return) retains arr's extent tagged with get_local's epoch, so CHKP3's
// interior interval-stabbing lookup catches this even though the exact
// stack_ptr_epochs lookup misses.
int *get_local(int i) {
    int arr[8];
    arr[i] = 42;
    return &arr[i]; // runtime offset -- not a single LEA3
}

void use(int *p) {
    int pad[8];  // match get_local's frame depth to defeat the range check
    pad[0] = 0;
    int y  = *p; // NOT caught by #670/#673 alone; caught by #675.
    (void)y;
    (void)pad;
}

int main(void) {
    int *p = get_local(3);
    use(p);
    return 0;
}
