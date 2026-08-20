// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #943: CHKNT propagation also covers a read-modify-write through a
// propagated pointer (`q[3] += 1`) -- to_assign() (src/parse.c) desugars
// this at parse time, before propagate_checked_bounds() has resolved `q`'s
// bounds, so the synthesized RMW store node needs a back-link
// (Node.checked_rmw_mirror) for the propagation pass's walk 3 to reach it
// and mirror the terminator-slot fact across after the fact.

int main(void) {
    int n                                     = 3;
    char *[[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q                                   = s;
    q[3] += 1; // RMW into the propagated terminator slot -- traps
    return 0;
}
