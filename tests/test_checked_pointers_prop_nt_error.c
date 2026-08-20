// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #943: CHKNT now propagates through a checked-bounds-propagation candidate
// (#919/#941/#942) -- a non-null store into the widened terminator slot
// through a propagated pointer traps exactly like the direct-access case
// (tests/test_checked_pointers_nt_fusion_o2.c and friends), because
// propagate_checked_bounds() now carries the terminator-slot fact
// (Obj.checked_prop_nt_elem) alongside the snapshot lo/hi it already
// propagated since #919. This is the ticket's own motivating example.

int main(void) {
    int n                                     = 3;
    char *[[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    char *q                                   = s;
    q[3] = 'x'; // now traps via CHKNT through `q`, same as through `s` directly
    return 0;
}
