// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --dangling-pointers
// Ticket #673: the follow-up to #670's dereference-time range check. p holds
// &x from get_local(), which has already returned by the time use() derefs
// it -- but the deref happens one frame *deeper* than the frame that got
// control back, so use()'s own frame has reclaimed the same stack memory
// (ptr >= sp holds again there) and the plain range check misses it. The new
// epoch-based frame-liveness check (stack_ptr_epochs vs live_epochs) catches
// this: x's frame epoch died with get_local(), regardless of what address
// range use()'s frame now occupies.
int *get_local(void) {
    int x = 42;
    return &x;
}

void use(int *p) {
    int y = *p; // NOT caught by the #670 range check alone; caught by #673.
    (void)y;
}

int main(void) {
    int *p = get_local();
    use(p);
    return 0;
}
