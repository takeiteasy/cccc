// CCCC_FLAGS: -3
// Ticket #671: CHKL's use-after-return check (stack instrumentation) used
// to key vm->stack_var_meta by bp-relative offset alone, in a single table
// shared by the whole program. Two different functions whose locals happen
// to land at the same offset (the common case -- e.g. each function's first
// local) collided: whichever function was registered second silently
// overwrote the other's declaration record. That produced a false "USE
// AFTER RETURN DETECTED" naming the wrong function's variable, exactly as
// below. Fixed by keying declarations by (scope_id, offset) and moving the
// runtime liveness check to actual address (bp+offset).
static void set(int *out) {
    *out = 42;
}

int main(void) {
    int n = 0;
    set(&n);
    return n;
}
