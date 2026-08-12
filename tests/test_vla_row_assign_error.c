// EXPECT_COMPILE_ERROR
// #974: whole-row assignment to a VLA lvalue must be rejected the same way
// it is for a fixed-size array (`v[0] = w[1];` on `int v[2][3]` already
// errors "not an lvalue" -- the equivalent VLA shape was silently accepted
// instead, since add_type's ND_ASSIGN check only tested TY_ARRAY, not
// TY_VLA). `v[0]` here is itself a VLA row (both dimensions variable), not a
// scalar, so this must fail to compile rather than run.
int main(void) {
    int n = 2, m = 3;
    int v[n][m];
    int w[n][m];
    v[0] = w[1]; // error: not an lvalue
    return 0;
}
