// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: n = 4;[\s\S]*int v\[n\];
// CCCC_REJECT_STDOUT: unsupported expr kind
// CCCC_REJECT_STDOUT: \+ \(int \*\)
//
// #964: ND_VLA_PTR had no serializer case at all -- the `v = alloca(tmp)`
// assignment declaration() lowers a VLA declarator to hit the default arm
// and printed `/* unsupported expr kind 41 */` where the alloca call's
// target belonged. Now it is replaced with a real `int v[n];` declaration,
// emitted in place (not hoisted) so it appears *after* the `n = 4;`
// assignment its length expression reads.
//
// The subscript lowering is asserted too: `node_is_pointerish()` didn't
// recognize TY_VLA, so `v[0]` serialized as a pointer plus a pointer
// (`(int *)v + (int *)(0 * 4)`) instead of a `(char *)`-based byte offset --
// REJECT_STDOUT catches a regression back to that shape.

int main(void) {
    int n = 4;
    int v[n];
    v[0] = 42;
    return v[0];
}
