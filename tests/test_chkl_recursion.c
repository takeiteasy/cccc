// CCCC_FLAGS: -3
// Ticket #671: recursive calls of the same function share one scope_id, so
// a declaration-level "current bp" field can't represent "is *this*
// activation still live" -- an inner recursive call's SCOPEIN overwrites
// it, and returning from that call left it stale, so accessing a local in
// the outer (still-running) activation after the inner call returned
// falsely tripped "USE AFTER RETURN DETECTED". Fixed by tracking liveness
// per activation, keyed by actual runtime address (bp+offset), populated at
// SCOPEIN and cleared at SCOPEOUT.
int fact(int n) {
    if (n <= 1)
        return 1;
    int r = fact(n - 1); // recursive call re-enters this function's scope_id
    return n * r;        // access to `r` here used to falsely abort
}

int main(void) {
    return fact(5) == 120 ? 42 : 1;
}
