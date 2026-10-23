// CCCC_FLAGS: -m --checked-pointers
// CCCC_EXPECT_STDOUT: __cccc_chknt
// CCCC_EXPECT_STDOUT: __atomic_compare_exchange_n
// -m output keeps the terminator-checked compare-exchange for an _Atomic
// ntarray element.
int main(void) {
    int n = 3;
    _Atomic char *[[cccc::ntarray, cccc::count(n)]] s =
        (char[4]){'a', 'b', 'c', 0};
    s[1] += 1;
    return s[1] == 'c' ? 42 : 1;
}
