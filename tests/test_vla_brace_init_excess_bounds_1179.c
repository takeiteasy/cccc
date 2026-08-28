// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -2
//
// #1179: VLA brace initialization is a deliberate CCCC-only extension (no
// reference compiler accepts `int v[n] = {...}`) -- kept, per #1179's
// decision, because its two concrete correctness objections are resolved.
// Excess initializers can't be diagnosed statically even in principle (if
// the length were an integer constant expression it would not be a VLA),
// but the resulting out-of-bounds store is caught by the same generic
// runtime bounds machinery as any other overrun, at `-2`/`-3` -- this pins
// that trap directly rather than only arguing it in the ticket.
int main(void) {
    int k    = 1;
    int u[k] = {1, 2, 3}; // writes u[1], u[2] past the 4-byte alloca'd VLA
    return u[0] + 41;
}
