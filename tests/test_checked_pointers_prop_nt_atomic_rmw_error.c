// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #943: CHKNT propagation also covers the `_Atomic` CAS-loop RMW desugar
// (to_assign()'s atomic branch, src/parse.c) through a propagated pointer --
// the same Node.checked_rmw_mirror back-link used for the non-atomic RMW
// case (test_checked_pointers_prop_nt_rmw_error.c) is set on this path too,
// pointing at the synthesized ND_CAS node instead of an ND_DEREF.

int main(void) {
    int n = 3;
    _Atomic char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    _Atomic char *q = s;
    q[3] += 1; // _Atomic RMW into the propagated terminator slot -- traps
    return 0;
}
