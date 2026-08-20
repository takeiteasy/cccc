// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #944: Checked C's `_Assume_bounds_cast` direction -- the propagation pass
// (#919/#941/#942) only ever WIDENS trust into a previously-unchecked
// target; it never verified that assigning a declared-checked source into a
// target that is ITSELF declared checked actually satisfies the target's
// own declared bounds. This is the ticket's own motivating example: `q`'s
// own declared count is 10, but the value assigned into it (`p`) only backs
// 4 elements -- CHKAB now traps at the assignment, not silently letting
// q[7] read past the real backing storage.

int main(void) {
    int *[[cccc::array, cccc::count(4)]] p = (int[4]){1, 2, 3, 4};
    int *[[cccc::array, cccc::count(10)]] q;
    q = p; // traps: p's bounds (4) don't imply q's own declared bounds (10)
    return q[7];
}
