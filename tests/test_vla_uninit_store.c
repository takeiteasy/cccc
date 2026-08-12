// CCCC_FLAGS: -3
// A store to a VLA element must not trip the uninitialized-variable-read
// detector (#980). The VLA declaration lowers to
// ND_ASSIGN(ND_VLA_PTR, alloca(...)); the store into the pointer's own
// frame slot must be marked initialized (MARKI) the same way a plain local
// is, or the later read of that slot (to compute the element address)
// fails CHKI with a false UNINITIALIZED VARIABLE READ.
int main(void) {
    int k = 1;
    int u[k];
    u[0] = 1;
    return 41 + u[0];
}
